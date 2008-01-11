/*
 * Copyright (C) 2007 OpenedHand Limited
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Neil Jagdish Patel <njp@o-hand.com>
 */


#include "astro-music-window.h"

#include <math.h>
#include <string.h>
#include <libastro-desktop/astro-defines.h>
#include <libastro-desktop/astro-application.h>
#include <libastro-desktop/astro-window.h>
#include <libastro-desktop/astro-behave.h>

#include "astro-reflection.h"

G_DEFINE_TYPE (AstroMusicWindow, astro_music_window, ASTRO_TYPE_WINDOW);

#define ASTRO_MUSIC_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj),\
        ASTRO_TYPE_MUSIC_WINDOW, AstroMusicWindowPrivate))

#define ALBUM_DIR PKGDATADIR"/albums"
#define ALBUM_SIZE (CSW()/4)

struct _AstroMusicWindowPrivate
{
  ClutterActor *albums;
  ClutterActor *label;

  ClutterActor *player;

  gint active;

  ClutterTimeline  *timeline;
  ClutterAlpha     *alpha;
  ClutterBehaviour *behave;
};

/* Public Functions */

/* Private functions */
#if 0
static void
ensure_layout (AstroMusicWindow *window)
{
#define VARIANCE 200
  AstroMusicWindowPrivate *priv;
  GList *l;
  gint groupx = 0;
  gint center = 0;
  gint i = 0;
  
  priv = window->priv;

  groupx = clutter_actor_get_x (CLUTTER_ACTOR (priv->albums));
  center = CSW()/2;

  l = clutter_container_get_children (CLUTTER_CONTAINER (priv->albums));
  for (l = l; l; l = l->next)
    {
      ClutterActor *cover = l->data;
      gint realx, diff;
      gfloat scale;

      realx = clutter_actor_get_x (cover) + groupx;
      
      if (realx > center && realx < CSW ())
        {
          diff = center - (realx - center);
        }
      else if (realx > 0 && realx <= center)
        {
          diff = realx;
        }
      else
        {
          diff = 0;
        }
  
      scale = (gfloat)diff/center;
      scale = 0.5 + (0.5 * scale);
      clutter_actor_set_scale (cover, scale, scale);
    
      i++;
    }
}
#endif

typedef struct
{
  gint x;
  gfloat scale;

} CoverTrans;

static void
ensure_layout (AstroMusicWindow *window)
{
  AstroMusicWindowPrivate *priv;
  GList *c;
  gint i = 0;

  priv = window->priv;

  c = clutter_container_get_children (CLUTTER_CONTAINER (priv->albums));
  for (c=c; c; c = c->next)
    {
      ClutterActor *cover = c->data;
      CoverTrans *trans = g_object_get_data (G_OBJECT (cover), "trans");

      if (i == priv->active)
        {
          trans->x = CSW ()/2;
          trans->scale = 1.0;
        }
      else if (i > priv->active)
        {
          gint diff;

          diff = i - priv->active;
          trans->x = (CSW()/2) + ((CSW()/4)*diff);
          if (diff > 3)
            trans->scale = 0.4;
          else
            trans->scale = 0.3 + (0.4 * (3-diff)/3);
        }
      else
        {
          gint diff;

          diff = priv->active - i;
          trans->x = (CSW()/2) - ((CSW()/4)*diff);
          if (diff > 3)
            trans->scale = 0.4;
          else
            trans->scale = 0.3 + (0.4 * (diff)/3);        }

      i++;
    }
}

static ClutterActor *
make_cover (const gchar *filename)
{
  GdkPixbuf *pixbuf;
  ClutterActor *texture;

  pixbuf = gdk_pixbuf_new_from_file_at_size (filename,
                                             ALBUM_SIZE, ALBUM_SIZE,
                                             NULL);
  if (!pixbuf)
    return NULL;

  texture = astro_reflection_new (pixbuf);

  g_object_set_data (G_OBJECT (texture), "trans", g_new0 (CoverTrans, 1));
  return texture;
}

static void
load_details (ClutterActor *cover, const gchar *leaf)
{
  gchar *details;
  gint i;

  details = g_strndup (leaf, strlen (leaf)-4);

  for (i = 0; i < strlen (details); i++)
    if (details[i] == '_') details[i] = ' ';

  clutter_actor_set_name (cover, details);
  g_free (details);
}

static void
load_albums (AstroMusicWindow *window)
{
  AstroMusicWindowPrivate *priv;
  GDir *dir;
  const gchar *leaf;
  GError *error = NULL;
  gint offset = 0;

  priv = window->priv;

  dir = g_dir_open (ALBUM_DIR, 0, &error);
  if (error)
    {
      g_warning ("Cannot load albums: %s", error->message);
      g_error_free (error);
      return;
    }
  
  while ((leaf = g_dir_read_name (dir)))
    {
      ClutterActor *cover;
      gchar *filename;

      if (!g_str_has_suffix (leaf, ".jpg"))
        continue;

      filename = g_build_filename (ALBUM_DIR, leaf, NULL);
      cover = make_cover (filename);

      if (!CLUTTER_IS_ACTOR (cover))
        {
          g_free (filename);
          continue;
        }
      load_details (cover, leaf);
      clutter_container_add_actor (CLUTTER_CONTAINER (priv->albums), cover);
      clutter_actor_set_position (cover, offset, 0);
      clutter_actor_show_all (cover);

      g_free (filename);

      offset += ALBUM_SIZE * 0.9;
    }
}

static void
astro_music_alpha (ClutterBehaviour *behave,
                   guint32           alpha_value,
                   AstroMusicWindow *window)
{
  AstroMusicWindowPrivate *priv;
  gfloat factor;
  GList *c;

  g_return_if_fail (ASTRO_IS_MUSIC_WINDOW (window));
  priv = window->priv;

  factor = (gfloat)alpha_value / CLUTTER_ALPHA_MAX_ALPHA;

  c = clutter_container_get_children (CLUTTER_CONTAINER (priv->albums));
  for (c=c; c; c = c->next)
    {
      ClutterActor *cover = c->data;
      CoverTrans *trans = g_object_get_data (G_OBJECT (cover), "trans");
      //gfloat currentscale, diffscale;
      gint currentx, diffx;
      
      currentx = clutter_actor_get_x (cover);
      if (currentx > trans->x)
        diffx = (currentx - trans->x) * -1;
      else
        diffx = trans->x - currentx;

      clutter_actor_set_x (cover, currentx + (gint)(diffx*factor));
      g_print ("%d\n", currentx + (gint)(diffx*factor));

    }
}

/* GObject stuff */
static void
astro_music_window_class_init (AstroMusicWindowClass *klass)
{
  GObjectClass        *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (gobject_class, sizeof (AstroMusicWindowPrivate));
}

static void
astro_music_window_init (AstroMusicWindow *window)
{
  AstroMusicWindowPrivate *priv;
  ClutterColor white = { 0xff, 0xff, 0xff, 0xff };

  priv = window->priv = ASTRO_MUSIC_WINDOW_GET_PRIVATE (window);

  priv->active = 0;

  priv->albums = clutter_group_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (window), priv->albums);
  clutter_actor_set_anchor_point_from_gravity (priv->albums, 
                                               CLUTTER_GRAVITY_WEST);
  clutter_actor_set_position (priv->albums, 
                              0, 
                              CSH()*0.4);

  load_albums (window);

  priv->label = clutter_label_new_full ("Sans 18", 
                                        "Jay Z - American Gangster",
                                        &white);
  clutter_label_set_line_wrap (CLUTTER_LABEL (priv->label), FALSE);
  clutter_container_add_actor (CLUTTER_CONTAINER (window), priv->label);
  clutter_actor_set_anchor_point_from_gravity (priv->label, 
                                               CLUTTER_GRAVITY_CENTER);
  clutter_actor_set_position (priv->label, CSW()/2, CSH()*0.8);

  ensure_layout (window);

  priv->timeline = clutter_timeline_new_for_duration (400);
  priv->alpha = clutter_alpha_new_full (priv->timeline,
                                        clutter_sine_inc_func,
                                        NULL, NULL);
  priv->behave = astro_behave_new (priv->alpha,
                                   (AstroBehaveAlphaFunc)astro_music_alpha,
                                   window);

  clutter_timeline_start (priv->timeline);

  clutter_actor_show_all (CLUTTER_ACTOR (window));
}

AstroWindow * 
astro_music_window_new (void)
{
  AstroWindow *music_window =  g_object_new (ASTRO_TYPE_MUSIC_WINDOW,
									                       NULL);

  return music_window;
}
