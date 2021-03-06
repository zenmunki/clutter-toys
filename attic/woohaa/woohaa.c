#include <clutter/clutter.h>
#include <clutter-gst/clutter-gst.h>
#include <gconf/gconf-client.h>
#include <gst/gst.h>
#include <sqlite3.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib/gstdio.h>

#ifdef USE_HELIX
#include <clutter-helix/clutter-helix.h>
#endif

#include "wh-slider-menu.h"
#include "wh-video-model.h"
#include "wh-video-model-row.h"
#include "wh-video-view.h"
#include "wh-db.h"
#include "wh-screen-video.h"
#include "wh-busy.h"
#include "wh-theme.h"
#include "util.h"

#define FONT "VistaSansBook 75px"
#define WOOHAA_GCONF_PREFIX   "/apps/woohaa"

typedef struct WooHaa
{
  ClutterActor      *screen_browse, *screen_video;
  WHVideoModel      *model;
  ClutterActor      *view;
  ClutterActor      *menu;
  ClutterActor      *busy;
  WHDB              *db;

  ClutterEffectTemplate *video_effect_tmpl;

  /* For thumbnailer */
  gint             tn_pending_child_pid;
  WHVideoModelRow *tn_pending_row;
  gint             tn_trys;
}
WooHaa;

gboolean
browse_input_cb (ClutterStage *stage,
		 ClutterEvent *event,
		 gpointer      user_data);

void 
video_input_cb (ClutterStage *stage,
		ClutterEvent *event,
		gpointer      user_data);

static void
thumbnail_find_empty (WooHaa *wh);

static gboolean
check_thumbnailer_child(gpointer data)
{
  WooHaa    *wh = (WooHaa *)data;
  GdkPixbuf *pixbuf;
  gint       status;

  if (wh->tn_pending_child_pid == 0 || wh->tn_pending_row == NULL)
    return FALSE;

  if (waitpid (wh->tn_pending_child_pid, 
	       &status, 
	       WNOHANG) != wh->tn_pending_child_pid)
    {
      /* Try again soon */
      wh->tn_trys++;

      if (wh->tn_trys > 5)
	{
	  g_warning("timed out making thumbnail");
	  /* Insert a blank pixbuf */
	  wh_video_model_row_set_thumbnail 
	    (wh->tn_pending_row, wh_theme_get_pixbuf("default-thumbnail"));
	  kill (wh->tn_pending_child_pid, SIGKILL);
	  waitpid (wh->tn_pending_child_pid, &status, 0);
	  goto cleanup;
	}
      return TRUE;
    }

  pixbuf = gdk_pixbuf_new_from_file ("/tmp/wh-thumb.png", NULL);

  if (pixbuf == NULL)
    {
      /* Insert a blank pixbuf */
      wh_video_model_row_set_thumbnail 
	(wh->tn_pending_row, wh_theme_get_pixbuf("default-thumbnail"));
      g_warning("failed to load pixbuf from thumbnailed");
      goto cleanup;
    }

  wh_video_model_row_set_thumbnail (wh->tn_pending_row, pixbuf);
  g_object_unref (pixbuf);

  wh_db_sync_row (wh->tn_pending_row);

 cleanup:
  g_object_unref(wh->tn_pending_row);
  wh->tn_pending_child_pid = 0;
  wh->tn_pending_row = NULL;
  wh->tn_trys = 0;
  g_remove ("/tmp/wh-thumb.png");

  thumbnail_find_empty (wh);

  /* All done */
  return FALSE;
}

gboolean
thumbnail_create (WooHaa *wh, WHVideoModelRow *row)
{
  gboolean result;
  gchar  **argv;

  if (wh->tn_pending_child_pid > 0 || wh->tn_pending_row != NULL)
    return TRUE;

  argv = g_new(gchar *, 4);
  argv[0] = g_strdup("wh-video-thumbnailer");
  argv[1] = g_strdup(wh_video_model_row_get_path(row));
  argv[2] = g_strdup("/tmp/wh-thumb.png");
  argv[3] = NULL;

  result = g_spawn_async (NULL,
			  argv,
			  NULL,
			  G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,
			  NULL,
			  NULL,
			  &wh->tn_pending_child_pid,
			  NULL);
  g_strfreev(argv);

  if (result == FALSE)
    {
      g_warning("failed to spawn wh-video-thumbnailer");

      wh_video_model_row_set_thumbnail 
	(row, wh_theme_get_pixbuf("default-thumbnail"));

      wh->tn_pending_row       = NULL;
      wh->tn_pending_child_pid = 0;

      return FALSE;
    }

  wh->tn_pending_row = row;
  g_object_ref(row);
  g_timeout_add (2500, check_thumbnailer_child, wh);

  return TRUE;
}

static gboolean
thumbnail_find_empty_foreach (WHVideoModel    *model,
			      WHVideoModelRow *row,
			      gpointer         data)
{
  if (wh_video_model_row_get_thumbnail (row) == NULL)
    {
      if (thumbnail_create ((WooHaa *)data, row))
	return FALSE;
    }

  return TRUE;
}

static void
thumbnail_find_empty (WooHaa *wh)
{
  if (!wh_screen_video_get_playing(WH_SCREEN_VIDEO(wh->screen_video))
      && wh->tn_pending_child_pid == 0)
    wh_video_model_foreach (wh->model, 
			    thumbnail_find_empty_foreach, 
			    (gpointer)wh); 
}

void
playback_finish_complete (ClutterActor *actor, WooHaa *wh)
{
  wh_video_view_enable_animation (WH_VIDEO_VIEW(wh->view), TRUE);
  thumbnail_find_empty(wh);
}

void
on_video_playback_start (WHScreenVideo *video,
			 WooHaa      *wh)
{
  clutter_actor_raise_top (wh->screen_video);

  clutter_actor_set_scale (wh->screen_browse, 0.7, 0.7);
  clutter_effect_scale (wh->video_effect_tmpl,
			wh->screen_browse,
			0.0,
			0.0,
			NULL,
			NULL);

  clutter_effect_fade (wh->video_effect_tmpl,
		       wh->screen_browse,
		       0,
		       NULL,
		       NULL);

  wh_video_view_enable_animation (WH_VIDEO_VIEW(wh->view), FALSE);

  /* Stop the busy 'cursor' */
  woohaa_busy_fade_out (WOOHAA_BUSY (wh->busy), 500);
}

void
on_video_playback_finish (WHScreenVideo *video,
			  WooHaa        *wh)
{
  WHVideoModelRow *row;

  g_signal_connect (clutter_stage_get_default(),
		    "key-release-event",
		    G_CALLBACK (browse_input_cb),
		    wh);

  /* update db */
  row = wh_video_view_get_selected (WH_VIDEO_VIEW(wh->view));
  wh_video_model_row_set_n_views (row, 
				  wh_video_model_row_get_n_views(row)+1);
  wh_video_model_row_set_vtime (row, time(NULL));
  wh_db_sync_row (row);

  clutter_actor_show (wh->screen_browse);

  clutter_effect_fade (wh->video_effect_tmpl,
		       wh->screen_browse,
		       0xff,
		       NULL,
		       NULL);

  clutter_effect_scale (wh->video_effect_tmpl,
			wh->screen_browse,
			1.0,
			1.0,
			NULL,
			NULL);

  clutter_effect_fade (wh->video_effect_tmpl,
		       wh->screen_video,
		       0,
		       (ClutterEffectCompleteFunc)playback_finish_complete,
		       wh);
}

gboolean
browse_input_cb (ClutterStage *stage,
		 ClutterEvent *event,
		 gpointer      user_data)
{
  WooHaa *wh = (WooHaa*)user_data;

  if (event->type == CLUTTER_KEY_RELEASE)
    {
      ClutterKeyEvent* kev = (ClutterKeyEvent *) event;

      switch (clutter_key_event_symbol (kev))
	{
	case CLUTTER_Left:
	  woohaa_slider_menu_advance (WOOHAA_SLIDER_MENU(wh->menu), -1);
	  thumbnail_find_empty (wh);
	  break;
	case CLUTTER_Right:
	  woohaa_slider_menu_advance (WOOHAA_SLIDER_MENU(wh->menu), 1);
	  thumbnail_find_empty (wh);
	  break;
	case CLUTTER_Up:
	  wh_video_view_advance (WH_VIDEO_VIEW(wh->view), -1);
	  break;
	case CLUTTER_Down:
	  wh_video_view_advance (WH_VIDEO_VIEW(wh->view), 1);
	  break;
	case CLUTTER_Return:
	  if (!wh_screen_video_activate (WH_SCREEN_VIDEO(wh->screen_video), 
					 WH_VIDEO_VIEW(wh->view)))
		  break;

	  g_signal_handlers_disconnect_by_func(clutter_stage_get_default(),
					       browse_input_cb,
					       wh);

	  clutter_effect_scale (wh->video_effect_tmpl,
				wh->screen_browse,
				0.0,
				0.0,
				NULL,
				NULL);

	  woohaa_busy_fade_in (WOOHAA_BUSY (wh->busy), 500);
	  break;
	case CLUTTER_Escape:
	case CLUTTER_q:
	  clutter_main_quit();
	  break;
	default:
	  break;
	}
    }

  return TRUE;
}

static gint
model_sort_mtime (WHVideoModelRow *rowa, WHVideoModelRow *rowb, gpointer data)
{
  time_t a, b;

  a = wh_video_model_row_get_age(rowa);
  b = wh_video_model_row_get_age(rowb);

  if (a > b) return -1;
  else if (a < b) return 1;
  else  return 0;
}

static gboolean  
model_recently_added_filter (WHVideoModel    *model,
			     WHVideoModelRow *row,
			     gpointer         data)
{
  return TRUE;
}

static void
view_recently_added_selected (WoohaaSliderMenu *menu,
			      ClutterActor      *actor,
			      gpointer           userdata)
{
  WooHaa *wh = (WooHaa*)userdata;

  wh_video_model_set_filter (wh->model, model_recently_added_filter, wh);
  wh_video_model_set_sort_func (wh->model, model_sort_mtime, NULL);
}

static gint
model_sort_alpha (WHVideoModelRow *rowa, WHVideoModelRow *rowb, gpointer data)
{
  return strcmp (wh_video_model_row_get_title(rowa),
		 wh_video_model_row_get_title(rowb));
}

#define FILTER_AF 0
#define FILTER_GL 1
#define FILTER_MR 2
#define FILTER_SZ 3

static gboolean  
model_alpha_filter (WHVideoModel    *model,
		    WHVideoModelRow *row,
		    gpointer         data)
{
  gint type = GPOINTER_TO_INT(data);
  gchar lo, hi;
  const gchar *title;

  title = wh_video_model_row_get_title(row);

  switch (type)
    {
    case FILTER_AF:
      lo = 'a'; hi = 'f';
      /* Also numerical titles */
      if (g_ascii_tolower(title[0]) >= '0' && g_ascii_tolower(title[0]) <= '9')
	return TRUE;
      break;
    case FILTER_GL:
      lo = 'g'; hi = 'l';
      break;
    case FILTER_MR:
      lo = 'm'; hi = 'r';
      break;
    case FILTER_SZ:
      lo = 's'; hi = 'z';
      break;
    default:
      lo = 's'; hi = 'z';
      break;
    }

  /* FIXME UTF8 */
  if (g_ascii_tolower(title[0]) >= lo && g_ascii_tolower(title[0]) <= hi)
    return TRUE;

  return FALSE;
}

static void
view_af_selected (WoohaaSliderMenu *menu,
		  ClutterActor      *actor,
		  gpointer           userdata)
{
  WooHaa *wh = (WooHaa*)userdata;
  
  wh_video_model_set_filter (wh->model, model_alpha_filter, 
			     GINT_TO_POINTER(FILTER_AF));
  wh_video_model_set_sort_func (wh->model, model_sort_alpha, NULL);
}

static void
view_gl_selected (WoohaaSliderMenu *menu,
		  ClutterActor      *actor,
		  gpointer           userdata)
{
  WooHaa *wh = (WooHaa*)userdata;

  wh_video_model_set_filter (wh->model, model_alpha_filter, 
			     GINT_TO_POINTER(FILTER_GL));
  wh_video_model_set_sort_func (wh->model, model_sort_alpha, NULL);
}

static void
view_mr_selected (WoohaaSliderMenu *menu,
		  ClutterActor      *actor,
		  gpointer           userdata)
{
  WooHaa *wh = (WooHaa*)userdata;

  wh_video_model_set_filter (wh->model, model_alpha_filter, 
			     GINT_TO_POINTER(FILTER_MR));
  wh_video_model_set_sort_func (wh->model, model_sort_alpha, NULL);
}

static void
view_sz_selected (WoohaaSliderMenu *menu,
		  ClutterActor      *actor,
		  gpointer           userdata)
{
  WooHaa *wh = (WooHaa*)userdata;

  wh_video_model_set_filter (wh->model, model_alpha_filter, 
			     GINT_TO_POINTER(FILTER_SZ));
  wh_video_model_set_sort_func (wh->model, model_sort_alpha, NULL);
}

static gint
model_sort_vtime (WHVideoModelRow *rowa, WHVideoModelRow *rowb, gpointer data)
{
  time_t a, b;

  a = wh_video_model_row_get_vtime(rowa);
  b = wh_video_model_row_get_vtime(rowb);

  if (a > b) return -1;
  else if (a < b) return 1;
  else  return 0;
}

static gboolean  
model_recently_viewed_filter (WHVideoModel    *model,
			      WHVideoModelRow *row,
			      gpointer         data)
{
  if (wh_video_model_row_get_n_views(row) == 0) 
    return FALSE;

  return TRUE;
}

void
view_recently_viewed_selected (WoohaaSliderMenu  *menu,
			       ClutterActor  *actor,
			       gpointer       userdata)
{
  WooHaa *wh = (WooHaa*)userdata;

  wh_video_model_set_filter (wh->model, model_recently_viewed_filter, wh);
  wh_video_model_set_sort_func (wh->model, model_sort_vtime, NULL);
}

static gboolean  
model_not_viewed_filter (WHVideoModel    *model,
			 WHVideoModelRow *row,
			 gpointer         data)
{
  if (wh_video_model_row_get_n_views(row) > 0) 
    return FALSE;

  return TRUE;
}

void
view_not_viewed_selected (WoohaaSliderMenu *menu,
			  ClutterActor      *actor,
			  gpointer           userdata)
{
  WooHaa *wh = (WooHaa*)userdata;

  wh_video_model_set_filter (wh->model, model_not_viewed_filter, wh);
  wh_video_model_set_sort_func (wh->model, model_sort_mtime, NULL);
}

static gint
model_sort_popular (WHVideoModelRow *rowa, 
		    WHVideoModelRow *rowb, 
		    gpointer         data)
{
  gint a, b;

  a = wh_video_model_row_get_n_views(rowa);
  b = wh_video_model_row_get_n_views(rowb);

  if (a > b) return -1;
  else if (a < b) return 1;
  else  return 0;
}


void
view_popular_selected (WoohaaSliderMenu *menu,
		       ClutterActor *actor,
		       gpointer      userdata)
{
  WooHaa *wh = (WooHaa*)userdata;

  wh_video_model_set_filter (wh->model, model_recently_viewed_filter, wh);
  wh_video_model_set_sort_func (wh->model, model_sort_popular, NULL);
}

void
on_db_row_created (WHDB *db, WHVideoModelRow *row, gpointer data)
{
  WooHaa *wh = (WooHaa *)data;

  wh_video_model_row_set_renderer (row, wh_video_row_renderer_new (row)); 
  wh_video_model_append_row (wh->model, row);
}

static void 
on_desktop_fade_complete (ClutterActor *actor, gpointer user_data)
{

  WooHaa *wh = (WooHaa *) user_data;
  WoohaaSliderMenu *menu = WOOHAA_SLIDER_MENU (wh->menu);

  /* Sort the model to correspond to insitial menu option */
  view_not_viewed_selected (menu, NULL, (gpointer)wh);
}

int
main (int argc, char *argv[])
{
  WooHaa        *wh;
  ClutterActor  *stage, *bg, *desktop;
  GConfClient   *client;
  GSList         *gconf_paths, *path;
  gchar         *font_str;
  gint           menu_h, browse_h;
  GError        *error = NULL;
  ClutterEffectTemplate *effect_template;
  ClutterColor   stage_color = { 0xff, 0xff, 0xf7, 0xff };

  gnome_vfs_init ();
#ifdef USE_HELIX
  clutter_helix_init (&argc, &argv);
#else
  gst_init (&argc, &argv);
#endif
  clutter_init (&argc, &argv);

  client = gconf_client_get_default ();
  gconf_paths = gconf_client_get_list (client,
				       WOOHAA_GCONF_PREFIX "/paths",
				       GCONF_VALUE_STRING,
				       &error);
  g_object_unref (client);

  if (gconf_paths == NULL)
    {
      g_printf("\n ***************************************************************************\n");
      g_printf("  To run woohaa you must set the GConf key; \n");
      g_printf("    '" WOOHAA_GCONF_PREFIX "/paths' \n");
      g_printf("  to a list of paths containing movie files.\n\n");
      g_printf("  To set the key, run;\n\n");
      g_printf("    gconftool-2 -s -t list --list-type=string " WOOHAA_GCONF_PREFIX "/paths \'[/path1,smb://10.1.1.1/path2]\'\n");
      g_printf("\n ***************************************************************************\n\n");

      exit(-1);
    }

  wh_theme_init();

  wh = g_new0(WooHaa, 1);

  wh->model = wh_video_model_new ();
  wh->db    = wh_db_new ();

  effect_template
    = clutter_effect_template_new (clutter_timeline_new_for_duration (1000),
				   CLUTTER_ALPHA_SINE_INC);

  stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, 800, 600); 
  g_object_set (stage, "fullscreen", TRUE, "cursor-visible", FALSE, NULL);
  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_color);

  bg = util_actor_from_file (PKGDATADIR"/bg.png", CSW(), CSH());

  clutter_group_add (CLUTTER_GROUP(stage), bg);
  clutter_actor_show (bg);

  clutter_actor_show (stage);

  /* General setup */

  wh->screen_browse = clutter_group_new();
  clutter_actor_set_size (wh->screen_browse, CSW(), CSH());

  menu_h     = CSH()/6;
  browse_h   = CSH() - 2 * menu_h;
  font_str   = g_strdup_printf("Sans %ipx", menu_h/2);
  
  wh->menu = woohaa_slider_menu_new (font_str);
  clutter_actor_set_size (wh->menu, CSW(), menu_h);
  clutter_actor_set_position (wh->menu, 0, menu_h/10);

  woohaa_slider_menu_add_option (WOOHAA_SLIDER_MENU (wh->menu), 
				 "Not Viewed",
				 view_not_viewed_selected, 
				 wh);

  woohaa_slider_menu_add_option (WOOHAA_SLIDER_MENU (wh->menu), 
				 "Recently Added",
				 view_recently_added_selected, 
				 wh);

  woohaa_slider_menu_add_option (WOOHAA_SLIDER_MENU (wh->menu), 
				 "A-F",
				 view_af_selected,
				 wh);

  woohaa_slider_menu_add_option (WOOHAA_SLIDER_MENU (wh->menu), 
				 "G-L",
				 view_gl_selected,
				 wh);

  woohaa_slider_menu_add_option (WOOHAA_SLIDER_MENU (wh->menu), 
				 "M-R",
				 view_mr_selected,
				 wh);

  woohaa_slider_menu_add_option (WOOHAA_SLIDER_MENU (wh->menu), 
				 "S-Z",
				 view_sz_selected,
				 wh);

  woohaa_slider_menu_add_option (WOOHAA_SLIDER_MENU (wh->menu), 
				 "Popular",
				 view_popular_selected,
				 wh);

  woohaa_slider_menu_add_option (WOOHAA_SLIDER_MENU (wh->menu), 
				 "Recently Viewed",
				 view_recently_viewed_selected, 
				 wh);

  woohaa_slider_menu_advance (WOOHAA_SLIDER_MENU (wh->menu), 0);

  clutter_group_add (CLUTTER_GROUP(wh->screen_browse), wh->menu);

  /* Video screen */


  wh->screen_video = wh_screen_video_new ();

  g_signal_connect(wh->screen_video, 
		   "playback-started",
		   G_CALLBACK(on_video_playback_start), 
		   wh);

  g_signal_connect(wh->screen_video, 
		   "playback-finished",
		   G_CALLBACK(on_video_playback_finish), 
		   wh);

  clutter_group_add (CLUTTER_GROUP(stage), wh->screen_video);

  /* Startup screen */

  desktop = util_texture_from_root_window ();
  clutter_group_add (CLUTTER_GROUP(stage), desktop);
  clutter_actor_show (desktop);

  wh->busy = woohaa_busy_new();
  clutter_group_add (CLUTTER_GROUP(stage), wh->busy);
  clutter_actor_hide (wh->busy);
  
  wh->video_effect_tmpl
    = clutter_effect_template_new (clutter_timeline_new_for_duration (500),
				   CLUTTER_ALPHA_SINE_INC);

  clutter_actor_set_scale (wh->busy, 0.2, 0.2);
  clutter_effect_scale (effect_template,
			wh->busy,
			1.0,
			1.0,
			NULL,
			NULL);

  clutter_effect_fade (effect_template,
		       desktop,
		       0,
		       on_desktop_fade_complete,
		       wh);
  clutter_effect_scale (effect_template,
			desktop,
			0.1,
			0.1,
			NULL,
			NULL);

  /* show stage for desktop zoom  */
  clutter_actor_show (stage);

  /* below actually spins main loop (for busy icon)  */

  g_signal_connect (wh->db, 
		    "row-created", 
		    G_CALLBACK (on_db_row_created), 
		    wh);

  for (path = gconf_paths; path != NULL; path = path->next)
    {
      char *uri = NULL;
      if (strstr (path->data, "://") == NULL)
        uri = g_filename_to_uri (path->data, NULL, NULL);
      wh_db_import_uri (wh->db, uri ? uri : path->data);
      g_free (uri);
      g_free (path->data);
    }
  g_slist_free (gconf_paths);

  /* view widget */

  /* FIXME: Should be able to do this before importing 
   * and get a smoother transition, but something appears wrong
   * with add signal.      
  */
  wh->view = wh_video_view_new (wh->model, 5);
  /* menu_h is CSH()/12 */
  clutter_actor_set_size (wh->view, CSW() - menu_h, browse_h);
  clutter_actor_set_position (wh->view, 
			      (CSW() - clutter_actor_get_width(wh->view)) / 2, 
			      menu_h + (menu_h/2) + (menu_h/6));

  clutter_group_add (CLUTTER_GROUP(wh->screen_browse), wh->view);

  clutter_group_add (CLUTTER_GROUP (stage), wh->screen_browse);

  /* Zoom to browse screen */

  clutter_actor_raise_top (wh->busy);

  clutter_actor_show_all (wh->screen_browse);

  g_signal_connect (stage, 
		    "key-release-event",
		    G_CALLBACK (browse_input_cb),
		    wh);

  g_remove ("/tmp/wh-thumb.png");
  thumbnail_find_empty(wh);

  clutter_main();

  g_remove ("/tmp/wh-thumb.png");

  return 0;
}
