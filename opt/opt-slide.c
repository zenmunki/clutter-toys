#include "opt.h"

G_DEFINE_TYPE (OptSlide, opt_slide, CLUTTER_TYPE_GROUP);

#define PERCENT_TO_PIXELS(p) \
   (( (p) * CLUTTER_STAGE_WIDTH() ) / 100)

struct OptSlidePrivate
{
  ClutterActor   *background;
  ClutterActor   *title;
  ClutterActor   *bg;
  GList          *bullets;
  OptShow        *show;
  OptTransition  *trans;
};

static void 
opt_slide_dispose (GObject *object)
{
  OptSlide *self = OPT_SLIDE(object); 

  if (self->priv)
    {
      if (self->priv->trans != NULL)
	g_object_unref(self->priv->trans);
      self->priv->trans = NULL;
    }

  G_OBJECT_CLASS (opt_slide_parent_class)->dispose (object);
}


static void 
opt_slide_finalize (GObject *object)
{
  OptSlide *self = OPT_SLIDE(object); 

  if (self->priv)
    {
      g_free(self->priv);
      self->priv = NULL;
    }

  G_OBJECT_CLASS (opt_slide_parent_class)->finalize (object);
}

static void
opt_slide_class_init (OptSlideClass *klass)
{
  GObjectClass        *object_class;
  ClutterActorClass *actor_class;

  object_class = (GObjectClass*) klass;
  actor_class = (ClutterActorClass*)klass;

  /* GObject */
  object_class->finalize     = opt_slide_finalize;
  object_class->dispose      = opt_slide_dispose;
}

static void
opt_slide_init (OptSlide *self)
{
  OptSlidePrivate *priv;

  priv = g_new0 (OptSlidePrivate, 1);

  self->priv  = priv;
}

OptSlide*
opt_slide_new (OptShow *show)
{
  OptSlide *slide;

  g_return_val_if_fail(OPT_IS_SHOW(show), NULL);

  slide = g_object_new (OPT_TYPE_SLIDE, NULL);

  slide->priv->show = show; 

  return slide;
}

void
opt_slide_set_title (OptSlide     *slide, 
		     const gchar  *title,
		     const gchar  *font,
		     ClutterColor *col)
{
  OptSlidePrivate *priv;
  gint             avail_w, border;
  gint             title_border_size;
  ClutterActor    *stage;

  g_return_if_fail(OPT_IS_SLIDE(slide));

  priv = slide->priv;

  if (priv->title != NULL)
    {
      clutter_group_remove (CLUTTER_GROUP(slide), priv->title);
      g_object_unref (priv->title);
    }

  if (font == NULL)
    {
      gchar *default_font = NULL;
      g_object_get (priv->show, "title-font", &default_font, NULL);
      priv->title = clutter_text_new_with_text (default_font, title);
      g_free (default_font);
    }
  else
    priv->title = clutter_text_new_with_text (font, title);

  clutter_group_add (CLUTTER_GROUP(slide), priv->title);

  g_object_get (priv->show, 
		"title-border-size", &title_border_size,
		NULL);

  stage = clutter_stage_get_default ();

  border = PERCENT_TO_PIXELS (title_border_size);

  avail_w = clutter_actor_get_width (stage) - (2 * border) ; 

  clutter_actor_set_size (CLUTTER_ACTOR(priv->title), avail_w, -1);

  clutter_text_set_color (CLUTTER_TEXT(priv->title), col);

  clutter_actor_set_position (priv->title, border, border);

  clutter_actor_show (priv->title);
}

void
get_next_bullet_offsets (OptSlide *slide,
			 gint     *x,
			 gint     *y,
			 gint     *max_width)
{
  OptSlidePrivate *priv;
  GList           *last_bullet_item;
  gint             title_bullet_pad, bullet_border_size, bullet_pad;

  priv = slide->priv;

  g_object_get (priv->show, 
		"title-bullet-pad", &title_bullet_pad,
		"bullet-pad", &bullet_pad,
		"bullet-border-size", &bullet_border_size,
		NULL);

  if ((last_bullet_item = g_list_last (priv->bullets)) == NULL)
    {
      *y = clutter_actor_get_y (priv->title)
	 + clutter_actor_get_height (priv->title);

      *y += PERCENT_TO_PIXELS (title_bullet_pad);
    }
  else
    {
      ClutterActor *last_bullet = CLUTTER_ACTOR(last_bullet_item->data);

      *y = clutter_actor_get_y (last_bullet)
	 + clutter_actor_get_height (last_bullet);

      *y += PERCENT_TO_PIXELS (bullet_pad);
    }

  *x = PERCENT_TO_PIXELS (bullet_border_size);

  *max_width = CLUTTER_STAGE_WIDTH() 
                  - (2 * PERCENT_TO_PIXELS (bullet_border_size)) ; 
}

void
opt_slide_add_bullet_text_item (OptSlide            *slide, 
				const gchar         *title,
				const gchar         *font,
				OptSlideBulletSymbol sym,
				ClutterColor        *col)
{
  OptSlidePrivate *priv;
  ClutterActor  *bullet, *symbol = NULL;
  gint             x, y, width, symbol_width = 0;

  priv = slide->priv;

  if (font == NULL)
    {
      gchar *default_font = NULL;

      g_object_get (priv->show, "bullet-font", &default_font, NULL);
      bullet = clutter_text_new_with_text (default_font, title);
      g_free (default_font);
    }
  else
    bullet = clutter_text_new_with_text (font, title);

  clutter_text_set_color (CLUTTER_TEXT(bullet), col);
  clutter_text_set_line_wrap (CLUTTER_TEXT (bullet), TRUE);

  get_next_bullet_offsets (slide, &x, &y, &width);

  symbol       = opt_show_bullet_clone (priv->show);
  symbol_width = 2 * clutter_actor_get_width (symbol);

  if (sym != OPT_BULLET_NONE)
    {
      clutter_group_add (CLUTTER_GROUP(slide), symbol);
      clutter_actor_set_position (symbol, x, y);
      clutter_actor_show(symbol);
    }

  x += symbol_width;

  clutter_actor_set_size (CLUTTER_ACTOR(bullet), width - symbol_width, -1);

  clutter_actor_set_position (bullet, x, y);
  clutter_group_add (CLUTTER_GROUP(slide), bullet);

  clutter_actor_show(bullet);


  priv->bullets = g_list_append(priv->bullets, bullet);
}

void
opt_slide_add_bullet (OptSlide *slide, ClutterActor *actor)
{
  OptSlidePrivate *priv;
  gint             x, y, width;

  priv = slide->priv;

  get_next_bullet_offsets (slide, &x, &y, &width);

  priv->bullets = g_list_append(priv->bullets, actor);

  clutter_group_add (CLUTTER_GROUP(slide), actor);

  clutter_actor_set_position (actor, 
				x + (width -clutter_actor_get_width(actor))
				                      /2, 
				y);

  clutter_actor_show(actor);
}

const ClutterActor*
opt_slide_get_title (OptSlide *slide)
{
  return slide->priv->title;
}

GList*
opt_slide_get_bullets (OptSlide *slide)
{
  return slide->priv->bullets;
}

void
opt_slide_set_transition (OptSlide *slide, OptTransition *trans)
{
  OptSlidePrivate *priv;

  priv = slide->priv;

  if (priv->trans == trans)
    return;

  if (priv->trans != NULL)
    g_object_unref(priv->trans);

  if (trans)
    {
      priv->trans = trans;
      g_object_ref(slide);
    }
}

OptTransition*
opt_slide_get_transition (OptSlide *slide)
{
  return slide->priv->trans;
}

void
opt_slide_set_background_pixbuf (OptSlide *slide, GdkPixbuf *background)
{
  OptSlidePrivate *priv;

  g_return_if_fail (background != NULL);

  priv = slide->priv;

  if (priv->background != NULL)
    clutter_actor_destroy (priv->background);

  priv->background = clutter_texture_new ();
  clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (priv->background),
                                     gdk_pixbuf_get_pixels (background),
                                     gdk_pixbuf_get_has_alpha (background),
                                     gdk_pixbuf_get_width (background),
                                     gdk_pixbuf_get_height (background),
                                     gdk_pixbuf_get_rowstride (background),
                                     gdk_pixbuf_get_n_channels (background), 
                                     0,
                                     NULL);
}

ClutterActor *
opt_slide_get_background_texture (OptSlide *slide)
{
  return slide->priv->background;
}
