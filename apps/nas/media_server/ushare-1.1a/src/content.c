/*
 * content.c : GeeXboX uShare content list
 * Originally developped for the GeeXboX project.
 * Copyright (C) 2005-2007 Alexis Saettler <asbin@asbin.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "content.h"

content_list *
content_add(content_list *list, const char *item, const char *name)
{
	if (!list)
	{
		list = (content_list*) malloc (sizeof(content_list));
		list->content = NULL;
		list->displayName = NULL;
		list->count = 0;
	}
	
	if (item && name)
	{
		list->count++;
		
		list->content = (char**) realloc (list->content, list->count * sizeof(char*));		
		if (!list->content)
		{
			perror ("error realloc");
			exit (2);
		}
		list->content[list->count-1] = strdup (item);
		
		list->displayName = (char**) realloc (list->displayName,  list->count * sizeof(char*));
		if (!list->displayName)
		{
			perror ("error realloc");
			exit (2);
		}
		list->displayName[list->count-1] = strdup (name);		
	}
  return list;
}

/*
 * Remove the n'th content (start from 0)
 */
content_list *
content_del(content_list *list, int n)
{
	int i;

	if (!list)
		return NULL;

	if ( (n >= list->count) || (n < 0))
		return list;

	if (list->content[n] || list->displayName[n])
	{
	if (list->content[n])
		free (list->content[n]);
	
		if (list->displayName[n])
			free (list->displayName[n]);
		
		for (i = n ; i < list->count - 1 ; i++)
		{
			list->content[i] = list->content[i+1];
			list->displayName[i] = list->displayName[i+1];
		}
			
		list->count--;
		list->content[list->count] = NULL;
		list->displayName[list->count] = NULL;
	}
	
  return list;
}

void
content_free(content_list *list)
{
  int i;
  if (!list)
    return;

  if (list->content)
  {
    for (i=0 ; i < list->count ; i++)
    {
      if (list->content[i])
        free (list->content[i]);
    }
    free (list->content);
  }

  /* free name. by HouXB, 21Jan11 */ 	
  if (list->displayName)
  {
    for (i=0 ; i < list->displayName; i++)
    {
      if (list->displayName[i])
        free (list->displayName[i]);
    }
    free (list->displayName);
  }
  free (list);
}
