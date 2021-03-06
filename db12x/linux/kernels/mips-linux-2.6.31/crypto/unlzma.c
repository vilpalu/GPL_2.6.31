/*
 * LZMA uncompresion module for pcomp
 * Copyright (C) 2009  Felix Fietkau <nbd@openwrt.org>
 *
 * Based on:
 *  Initial Linux kernel adaptation
 *  Copyright (C) 2006  Alain < alain@knaff.lu >
 *
 *  Based on small lzma deflate implementation/Small range coder
 *  implementation for lzma.
 *  Copyright (C) 2006  Aurelien Jacobs < aurel@gnuage.org >
 *
 *  Based on LzmaDecode.c from the LZMA SDK 4.22 (http://www.7-zip.org/)
 *  Copyright (C) 1999-2005  Igor Pavlov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * FIXME: the current implementation assumes that the caller will
 * not free any output buffers until the whole decompression has been
 * completed. This is necessary, because LZMA looks back at old output
 * instead of doing a separate dictionary allocation, which saves RAM.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include <crypto/internal/compress.h>
#include <net/netlink.h>
#include "unlzma.h"

static int instance = 0;

struct unlzma_buffer {
	int offset;
	int size;
	u8 *ptr;
};

struct unlzma_ctx {
	struct task_struct *thread;
	wait_queue_head_t next_req;
	wait_queue_head_t req_done;
	struct mutex mutex;
	bool waiting;
	bool active;
	bool cancel;

	const u8 *next_in;
	int avail_in;

	u8 *next_out;
	int avail_out;

	/* reader state */
	u32 code;
	u32 range;
	u32 bound;

	/* writer state */
	u8 previous_byte;
	ssize_t pos;
	int buf_full;
	int n_buffers;
	int buffers_max;
	struct unlzma_buffer *buffers;

	/* cstate */
	int state;
	u32 rep0, rep1, rep2, rep3;

	u32 dict_size;

	void *workspace;
	int workspace_size;
};

static inline bool
unlzma_should_stop(struct unlzma_ctx *ctx)
{
	return unlikely(kthread_should_stop() || ctx->cancel);
}

static void
get_buffer(struct unlzma_ctx *ctx)
{
	struct unlzma_buffer *bh;

	BUG_ON(ctx->n_buffers >= ctx->buffers_max);
	bh = &ctx->buffers[ctx->n_buffers++];
	bh->ptr = ctx->next_out;
	bh->offset = ctx->pos;
	bh->size = ctx->avail_out;
	ctx->buf_full = 0;
}

static void
unlzma_request_buffer(struct unlzma_ctx *ctx, int *avail)
{
	do {
		ctx->waiting = true;
		mutex_unlock(&ctx->mutex);
		wake_up(&ctx->req_done);
		if (wait_event_interruptible(ctx->next_req,
			unlzma_should_stop(ctx) || (*avail > 0)))
			schedule();
		mutex_lock(&ctx->mutex);
	} while (*avail <= 0 && !unlzma_should_stop(ctx));

	if (!unlzma_should_stop(ctx) && ctx->buf_full)
		get_buffer(ctx);
}

static u8
rc_read(struct unlzma_ctx *ctx)
{
	if (unlikely(ctx->avail_in <= 0))
		unlzma_request_buffer(ctx, &ctx->avail_in);

	if (unlzma_should_stop(ctx))
		return 0;

	ctx->avail_in--;
	return *(ctx->next_in++);
}


static inline void
rc_get_code(struct unlzma_ctx *ctx)
{
	ctx->code = (ctx->code << 8) | rc_read(ctx);
}

static void
rc_normalize(struct unlzma_ctx *ctx)
{
	if (ctx->range < (1 << RC_TOP_BITS)) {
		ctx->range <<= 8;
		rc_get_code(ctx);
	}
}

static int
rc_is_bit_0(struct unlzma_ctx *ctx, u16 *p)
{
	rc_normalize(ctx);
	ctx->bound = *p * (ctx->range >> RC_MODEL_TOTAL_BITS);
	return ctx->code < ctx->bound;
}

static void
rc_update_bit_0(struct unlzma_ctx *ctx, u16 *p)
{
	ctx->range = ctx->bound;
	*p += ((1 << RC_MODEL_TOTAL_BITS) - *p) >> RC_MOVE_BITS;
}

static void
rc_update_bit_1(struct unlzma_ctx *ctx, u16 *p)
{
	ctx->range -= ctx->bound;
	ctx->code -= ctx->bound;
	*p -= *p >> RC_MOVE_BITS;
}

static bool
rc_get_bit(struct unlzma_ctx *ctx, u16 *p, int *symbol)
{
	if (rc_is_bit_0(ctx, p)) {
		rc_update_bit_0(ctx, p);
		*symbol *= 2;
		return 0;
	} else {
		rc_update_bit_1(ctx, p);
		*symbol = *symbol * 2 + 1;
		return 1;
	}
}

static int
rc_direct_bit(struct unlzma_ctx *ctx)
{
	rc_normalize(ctx);
	ctx->range >>= 1;
	if (ctx->code >= ctx->range) {
		ctx->code -= ctx->range;
		return 1;
	}
	return 0;
}

static void
rc_bit_tree_decode(struct unlzma_ctx *ctx, u16 *p, int num_levels, int *symbol)
{
	int i = num_levels;

	*symbol = 1;
	while (i--)
		rc_get_bit(ctx, p + *symbol, symbol);
	*symbol -= 1 << num_levels;
}

static u8
peek_old_byte(struct unlzma_ctx *ctx, u32 offs)
{
	struct unlzma_buffer *bh = &ctx->buffers[ctx->n_buffers - 1];
	int i = ctx->n_buffers;
	u32 pos;

	if (!ctx->n_buffers) {
		printk(KERN_ERR "unlzma/%s: no buffer\n", __func__);
		goto error;
	}

	pos = ctx->pos - offs;
	if (unlikely(pos >= ctx->dict_size))
		pos = ~pos & (ctx->dict_size - 1);

	while (bh->offset > pos) {
		bh--;
		i--;
		if (!i) {
			printk(KERN_ERR "unlzma/%s: position %d out of range\n", __func__, pos);
			goto error;
		}
	}

	pos -= bh->offset;
	if (pos >= bh->size) {
		printk(KERN_ERR "unlzma/%s: position %d out of range\n", __func__, pos);
		goto error;
	}

	return bh->ptr[pos];

error:
	ctx->cancel = true;
	return 0;
}

static void
write_byte(struct unlzma_ctx *ctx, u8 byte)
{
	if (unlikely(ctx->avail_out <= 0)) {
		unlzma_request_buffer(ctx, &ctx->avail_out);
	}

	if (!ctx->avail_out)
		return;

	ctx->previous_byte = byte;
	*(ctx->next_out++) = byte;
	ctx->avail_out--;
	if (ctx->avail_out == 0)
		ctx->buf_full = 1;
	ctx->pos++;
}


static inline void
copy_byte(struct unlzma_ctx *ctx, u32 offs)
{
	write_byte(ctx, peek_old_byte(ctx, offs));
}

static void
copy_bytes(struct unlzma_ctx *ctx, u32 rep0, int len)
{
	do {
		copy_byte(ctx, rep0);
		len--;
		if (unlzma_should_stop(ctx))
			break;
	} while (len != 0);
}

static void
process_bit0(struct unlzma_ctx *ctx, u16 *p, int pos_state, u16 *prob,
             int lc, u32 literal_pos_mask)
{
	int mi = 1;
	rc_update_bit_0(ctx, prob);
	prob = (p + LZMA_LITERAL +
		(LZMA_LIT_SIZE
		 * (((ctx->pos & literal_pos_mask) << lc)
		    + (ctx->previous_byte >> (8 - lc))))
		);

	if (ctx->state >= LZMA_NUM_LIT_STATES) {
		int match_byte = peek_old_byte(ctx, ctx->rep0);
		do {
			u16 bit;
			u16 *prob_lit;

			match_byte <<= 1;
			bit = match_byte & 0x100;
			prob_lit = prob + 0x100 + bit + mi;
			if (rc_get_bit(ctx, prob_lit, &mi) != !!bit)
				break;
		} while (mi < 0x100);
	}
	while (mi < 0x100) {
		u16 *prob_lit = prob + mi;
		rc_get_bit(ctx, prob_lit, &mi);
	}
	write_byte(ctx, mi);
	if (ctx->state < 4)
		ctx->state = 0;
	else if (ctx->state < 10)
		ctx->state -= 3;
	else
		ctx->state -= 6;
}

static void
process_bit1(struct unlzma_ctx *ctx, u16 *p, int pos_state, u16 *prob)
{
	int offset;
	u16 *prob_len;
	int num_bits;
	int len;

	rc_update_bit_1(ctx, prob);
	prob = p + LZMA_IS_REP + ctx->state;
	if (rc_is_bit_0(ctx, prob)) {
		rc_update_bit_0(ctx, prob);
		ctx->rep3 = ctx->rep2;
		ctx->rep2 = ctx->rep1;
		ctx->rep1 = ctx->rep0;
		ctx->state = ctx->state < LZMA_NUM_LIT_STATES ? 0 : 3;
		prob = p + LZMA_LEN_CODER;
	} else {
		rc_update_bit_1(ctx, prob);
		prob = p + LZMA_IS_REP_G0 + ctx->state;
		if (rc_is_bit_0(ctx, prob)) {
			rc_update_bit_0(ctx, prob);
			prob = (p + LZMA_IS_REP_0_LONG
				+ (ctx->state <<
				   LZMA_NUM_POS_BITS_MAX) +
				pos_state);
			if (rc_is_bit_0(ctx, prob)) {
				rc_update_bit_0(ctx, prob);

				ctx->state = ctx->state < LZMA_NUM_LIT_STATES ?
					9 : 11;
				copy_byte(ctx, ctx->rep0);
				return;
			} else {
				rc_update_bit_1(ctx, prob);
			}
		} else {
			u32 distance;

			rc_update_bit_1(ctx, prob);
			prob = p + LZMA_IS_REP_G1 + ctx->state;
			if (rc_is_bit_0(ctx, prob)) {
				rc_update_bit_0(ctx, prob);
				distance = ctx->rep1;
			} else {
				rc_update_bit_1(ctx, prob);
				prob = p + LZMA_IS_REP_G2 + ctx->state;
				if (rc_is_bit_0(ctx, prob)) {
					rc_update_bit_0(ctx, prob);
					distance = ctx->rep2;
				} else {
					rc_update_bit_1(ctx, prob);
					distance = ctx->rep3;
					ctx->rep3 = ctx->rep2;
				}
				ctx->rep2 = ctx->rep1;
			}
			ctx->rep1 = ctx->rep0;
			ctx->rep0 = distance;
		}
		ctx->state = ctx->state < LZMA_NUM_LIT_STATES ? 8 : 11;
		prob = p + LZMA_REP_LEN_CODER;
	}

	prob_len = prob + LZMA_LEN_CHOICE;
	if (rc_is_bit_0(ctx, prob_len)) {
		rc_update_bit_0(ctx, prob_len);
		prob_len = (prob + LZMA_LEN_LOW
			    + (pos_state <<
			       LZMA_LEN_NUM_LOW_BITS));
		offset = 0;
		num_bits = LZMA_LEN_NUM_LOW_BITS;
	} else {
		rc_update_bit_1(ctx, prob_len);
		prob_len = prob + LZMA_LEN_CHOICE_2;
		if (rc_is_bit_0(ctx, prob_len)) {
			rc_update_bit_0(ctx, prob_len);
			prob_len = (prob + LZMA_LEN_MID
				    + (pos_state <<
				       LZMA_LEN_NUM_MID_BITS));
			offset = 1 << LZMA_LEN_NUM_LOW_BITS;
			num_bits = LZMA_LEN_NUM_MID_BITS;
		} else {
			rc_update_bit_1(ctx, prob_len);
			prob_len = prob + LZMA_LEN_HIGH;
			offset = ((1 << LZMA_LEN_NUM_LOW_BITS)
				  + (1 << LZMA_LEN_NUM_MID_BITS));
			num_bits = LZMA_LEN_NUM_HIGH_BITS;
		}
	}

	rc_bit_tree_decode(ctx, prob_len, num_bits, &len);
	len += offset;

	if (ctx->state < 4) {
		int pos_slot;

		ctx->state += LZMA_NUM_LIT_STATES;
		prob =
			p + LZMA_POS_SLOT +
			((len <
			  LZMA_NUM_LEN_TO_POS_STATES ? len :
			  LZMA_NUM_LEN_TO_POS_STATES - 1)
			 << LZMA_NUM_POS_SLOT_BITS);
		rc_bit_tree_decode(ctx, prob,
				   LZMA_NUM_POS_SLOT_BITS,
				   &pos_slot);
		if (pos_slot >= LZMA_START_POS_MODEL_INDEX) {
			int i, mi;
			num_bits = (pos_slot >> 1) - 1;
			ctx->rep0 = 2 | (pos_slot & 1);
			if (pos_slot < LZMA_END_POS_MODEL_INDEX) {
				ctx->rep0 <<= num_bits;
				prob = p + LZMA_SPEC_POS +
					ctx->rep0 - pos_slot - 1;
			} else {
				num_bits -= LZMA_NUM_ALIGN_BITS;
				while (num_bits--)
					ctx->rep0 = (ctx->rep0 << 1) |
						rc_direct_bit(ctx);
				prob = p + LZMA_ALIGN;
				ctx->rep0 <<= LZMA_NUM_ALIGN_BITS;
				num_bits = LZMA_NUM_ALIGN_BITS;
			}
			i = 1;
			mi = 1;
			while (num_bits--) {
				if (rc_get_bit(ctx, prob + mi, &mi))
					ctx->rep0 |= i;
				i <<= 1;
			}
		} else
			ctx->rep0 = pos_slot;
		if (++(ctx->rep0) == 0)
			return;
	}

	len += LZMA_MATCH_MIN_LEN;

	copy_bytes(ctx, ctx->rep0, len);
}


static int
do_unlzma(struct unlzma_ctx *ctx)
{
	u8 hdr_buf[sizeof(struct lzma_header)];
	struct lzma_header *header = (struct lzma_header *)hdr_buf;
	u32 pos_state_mask;
	u32 literal_pos_mask;
	int lc, pb, lp;
	int num_probs;
	int i, mi;
	u16 *p;

	for (i = 0; i < sizeof(struct lzma_header); i++) {
		hdr_buf[i] = rc_read(ctx);
	}

	ctx->n_buffers = 0;
	ctx->pos = 0;
	get_buffer(ctx);
	ctx->active = true;
	ctx->state = 0;
	ctx->rep0 = ctx->rep1 = ctx->rep2 = ctx->rep3 = 1;

	ctx->previous_byte = 0;
	ctx->code = 0;
	ctx->range = 0xFFFFFFFF;

	ctx->dict_size = le32_to_cpu(header->dict_size);

	if (header->pos >= (9 * 5 * 5))
		return -1;

	mi = 0;
	lc = header->pos;
	while (lc >= 9) {
		mi++;
		lc -= 9;
	}
	pb = 0;
	lp = mi;
	while (lp >= 5) {
		pb++;
		lp -= 5;
	}
	pos_state_mask = (1 << pb) - 1;
	literal_pos_mask = (1 << lp) - 1;

	if (ctx->dict_size == 0)
		ctx->dict_size = 1;

	num_probs = LZMA_BASE_SIZE + (LZMA_LIT_SIZE << (lc + lp));
	if (ctx->workspace_size < num_probs * sizeof(*p)) {
		if (ctx->workspace)
			vfree(ctx->workspace);
		ctx->workspace_size = num_probs * sizeof(*p);
		ctx->workspace = vmalloc(ctx->workspace_size);
	}
	p = (u16 *) ctx->workspace;
	if (!p)
		return -1;

	num_probs = LZMA_LITERAL + (LZMA_LIT_SIZE << (lc + lp));
	for (i = 0; i < num_probs; i++)
		p[i] = (1 << RC_MODEL_TOTAL_BITS) >> 1;

	for (i = 0; i < 5; i++)
		rc_get_code(ctx);

	while (1) {
		int pos_state =	ctx->pos & pos_state_mask;
		u16 *prob = p + LZMA_IS_MATCH +
			(ctx->state << LZMA_NUM_POS_BITS_MAX) + pos_state;
		if (rc_is_bit_0(ctx, prob))
			process_bit0(ctx, p, pos_state, prob,
				     lc, literal_pos_mask);
		else {
			process_bit1(ctx, p, pos_state, prob);
			if (ctx->rep0 == 0)
				break;
		}
		if (unlzma_should_stop(ctx))
			break;
	}
	if (likely(!unlzma_should_stop(ctx)))
		rc_normalize(ctx);

	return ctx->pos;
}


static void
unlzma_reset_buf(struct unlzma_ctx *ctx)
{
	ctx->avail_in = 0;
	ctx->next_in = NULL;
	ctx->avail_out = 0;
	ctx->next_out = NULL;
}

static int
unlzma_thread(void *data)
{
	struct unlzma_ctx *ctx = data;

	mutex_lock(&ctx->mutex);
	do {
		if (do_unlzma(ctx) < 0)
			ctx->pos = 0;
		unlzma_reset_buf(ctx);
		ctx->cancel = false;
		ctx->active = false;
	} while (!kthread_should_stop());
	mutex_unlock(&ctx->mutex);
	return 0;
}


static int
unlzma_init(struct crypto_tfm *tfm)
{
	return 0;
}

static void
unlzma_cancel(struct unlzma_ctx *ctx)
{
	unlzma_reset_buf(ctx);

	if (!ctx->active)
		return;

	ctx->cancel = true;
	do {
		mutex_unlock(&ctx->mutex);
		wake_up(&ctx->next_req);
		schedule();
		mutex_lock(&ctx->mutex);
	} while (ctx->cancel);
}


static void
unlzma_exit(struct crypto_tfm *tfm)
{
	struct unlzma_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->thread) {
		unlzma_cancel(ctx);
		kthread_stop(ctx->thread);
		ctx->thread = NULL;
		if (ctx->buffers)
			kfree(ctx->buffers);
		ctx->buffers_max = 0;
		ctx->buffers = NULL;
	}
}

static int
unlzma_decompress_setup(struct crypto_pcomp *tfm, void *p, unsigned int len)
{
	struct unlzma_ctx *ctx = crypto_tfm_ctx(crypto_pcomp_tfm(tfm));
	struct nlattr *tb[UNLZMA_DECOMP_MAX + 1];
	int ret = 0;

	if (ctx->thread)
		return -EINVAL;

	if (!p)
		return -EINVAL;

	ret = nla_parse(tb, UNLZMA_DECOMP_MAX, p, len, NULL);
	if (ret)
		return ret;

	if (!tb[UNLZMA_DECOMP_OUT_BUFFERS])
		return -EINVAL;

	if (ctx->buffers_max && (ctx->buffers_max <
	    nla_get_u32(tb[UNLZMA_DECOMP_OUT_BUFFERS]))) {
		kfree(ctx->buffers);
		ctx->buffers_max = 0;
		ctx->buffers = NULL;
	}
	if (!ctx->buffers) {
		ctx->buffers_max = nla_get_u32(tb[UNLZMA_DECOMP_OUT_BUFFERS]);
		ctx->buffers = kzalloc(sizeof(struct unlzma_buffer) * ctx->buffers_max, GFP_KERNEL);
	}
	if (!ctx->buffers)
		return -ENOMEM;

	ctx->waiting = false;
	mutex_init(&ctx->mutex);
	init_waitqueue_head(&ctx->next_req);
	init_waitqueue_head(&ctx->req_done);
	ctx->thread = kthread_run(unlzma_thread, ctx, "unlzma/%d", instance++);
	if (IS_ERR(ctx->thread)) {
		ret = PTR_ERR(ctx->thread);
		ctx->thread = NULL;
	}

	return ret;
}

static int
unlzma_decompress_init(struct crypto_pcomp *tfm)
{
	return 0;
}

static void
unlzma_wait_complete(struct unlzma_ctx *ctx, bool finish)
{
	DEFINE_WAIT(__wait);

	do {
		wake_up(&ctx->next_req);
		prepare_to_wait(&ctx->req_done, &__wait, TASK_INTERRUPTIBLE);
		mutex_unlock(&ctx->mutex);
		schedule();
		mutex_lock(&ctx->mutex);
	} while (!ctx->waiting && ctx->active);
	finish_wait(&ctx->req_done, &__wait);
}

static int
unlzma_decompress_update(struct crypto_pcomp *tfm, struct comp_request *req)
{
	struct unlzma_ctx *ctx = crypto_tfm_ctx(crypto_pcomp_tfm(tfm));
	size_t pos = 0;

	mutex_lock(&ctx->mutex);
	if (!ctx->active && !req->avail_in)
		goto out;

	pos = ctx->pos;
	ctx->waiting = false;
	ctx->next_in = req->next_in;
	ctx->avail_in = req->avail_in;
	ctx->next_out = req->next_out;
	ctx->avail_out = req->avail_out;

	unlzma_wait_complete(ctx, false);

	req->next_in = ctx->next_in;
	req->avail_in = ctx->avail_in;
	req->next_out = ctx->next_out;
	req->avail_out = ctx->avail_out;
	ctx->next_in = 0;
	ctx->avail_in = 0;
	pos = ctx->pos - pos;

out:
	mutex_unlock(&ctx->mutex);
	if (ctx->cancel)
		return -EINVAL;

	return pos;
}

static int
unlzma_decompress_final(struct crypto_pcomp *tfm, struct comp_request *req)
{
	struct unlzma_ctx *ctx = crypto_tfm_ctx(crypto_pcomp_tfm(tfm));
	int ret = 0;

	/* cancel pending operation */
	mutex_lock(&ctx->mutex);
	if (ctx->active) {
		// ret = -EINVAL;
		unlzma_cancel(ctx);
	}
	ctx->pos = 0;
	mutex_unlock(&ctx->mutex);
	return ret;
}


static struct pcomp_alg unlzma_alg = {
	.decompress_setup	= unlzma_decompress_setup,
	.decompress_init	= unlzma_decompress_init,
	.decompress_update	= unlzma_decompress_update,
	.decompress_final	= unlzma_decompress_final,

	.base			= {
		.cra_name	= "lzma",
		.cra_flags	= CRYPTO_ALG_TYPE_PCOMPRESS,
		.cra_ctxsize	= sizeof(struct unlzma_ctx),
		.cra_module	= THIS_MODULE,
		.cra_init	= unlzma_init,
		.cra_exit	= unlzma_exit,
	}
};

static int __init
unlzma_mod_init(void)
{
	return crypto_register_pcomp(&unlzma_alg);
}

static void __exit
unlzma_mod_exit(void)
{
	crypto_unregister_pcomp(&unlzma_alg);
}

module_init(unlzma_mod_init);
module_exit(unlzma_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZMA Decompression Algorithm");
MODULE_AUTHOR("Felix Fietkau <nbd@openwrt.org>");
