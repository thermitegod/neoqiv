/*
  Module       : image.c
  Purpose      : Routines dealing with image display
  More         : see qiv README
  Policy       : GNU GPL
  Homepage     : http://qiv.spiegl.de/
  Original     : http://www.klografx.net/qiv/
*/

#include "qiv.h"
#include <gdk/gdkx.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

static void setup_win(qiv_image *);
// static void setup_magnify(qiv_image *, qiv_mgl *); // [lc]
static int used_masks_before = 0;
static struct timeval load_before, load_after;
static double load_elapsed;

Imlib_Image im_from_pixbuf_loader(char *image_name, int *has_alpha)
{
    GError *error = NULL;
    GdkPixbuf *pixbuf_ori;
    GdkPixbuf *pixbuf;
    char *argbdata;
    guchar *pixels;
    guchar *pixels_ori;
    int i, j, k, rs;
    int pb_w, pb_h;
    Imlib_Image *im = NULL;
    const gchar *gdk_orientation = NULL;
#ifdef SUPPORT_LCMS
    char *icc_profile;
    cmsHPROFILE h_emb_profile;
    cmsHTRANSFORM h_emb_transform;
#endif

    pixbuf_ori = gdk_pixbuf_new_from_file(image_name, &error);
    if (error != NULL)
    {
        /* Report error to user, and free error */
        fprintf(stderr, "Unable to read file: %s\n", error->message);
        g_error_free(error);
    }
    else
    {
#if GDK_PIXBUF_MINOR >= 12
        if (autorotate)
        {
            gdk_orientation = gdk_pixbuf_get_option(pixbuf_ori, "orientation");
            if (gdk_orientation)
            {
#ifdef DEBUG
                printf("orientation %s\n", gdk_orientation);
#endif
                pixbuf = gdk_pixbuf_apply_embedded_orientation(pixbuf_ori);
                g_object_unref(pixbuf_ori);
                pixbuf_ori = pixbuf;
            }
        }
#else
#warning autoration needs at least gdk version 2.12
#endif

        *has_alpha = (gdk_pixbuf_get_n_channels(pixbuf_ori) == 4) ? 1 : 0;

        pixels_ori = gdk_pixbuf_get_pixels(pixbuf_ori);
        /* create checkboard if image has transparency */
        if (*has_alpha)
        {
            pixbuf = gdk_pixbuf_composite_color_simple(
                pixbuf_ori, gdk_pixbuf_get_width(pixbuf_ori), gdk_pixbuf_get_height(pixbuf_ori),
                GDK_INTERP_NEAREST, 255, 0x08, 0x00666666, 0x00aaaaaa);

            pixels = gdk_pixbuf_get_pixels(pixbuf);
        }
        else
        {
            pixbuf = pixbuf_ori;
            pixels = pixels_ori;
        }

#ifdef DEBUG
        printf("channels %i\n", gdk_pixbuf_get_n_channels(pixbuf));
        printf("rowstride %i\n", gdk_pixbuf_get_rowstride(pixbuf));
#endif

        pb_w = gdk_pixbuf_get_width(pixbuf);
        pb_h = gdk_pixbuf_get_height(pixbuf);

        argbdata = malloc(4 * pb_w * pb_h);

        /* create imlib2 compatible data */
        if (*has_alpha)
        {
            for (i = 0; i < pb_w * pb_h * 4; i = i + 4)
            {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                argbdata[i + 0] = pixels[i + 2]; // B
                argbdata[i + 1] = pixels[i + 1]; // G
                argbdata[i + 2] = pixels[i]; // R
                // keep old alpha values
                argbdata[i + 3] = pixels_ori[i + 3]; // Alpha
#else
                // BIG_ENDIAN
                argbdata[i + 3] = pixels[i + 2]; // B
                argbdata[i + 2] = pixels[i + 1]; // G
                argbdata[i + 1] = pixels[i]; // R
                // keep old alpha values
                argbdata[i + 0] = pixels_ori[i + 3]; // Alpha
#endif
            }
        }
        else
        {
            rs = gdk_pixbuf_get_rowstride(pixbuf);
            k = 0;
            for (i = 0; i < pb_h; i++)
            {
                for (j = 0; j < pb_w * 3; j += 3)
                {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                    argbdata[k++] = pixels[i * rs + j + 2]; // B
                    argbdata[k++] = pixels[i * rs + j + 1]; // G
                    argbdata[k++] = pixels[i * rs + j]; // R
                    argbdata[k++] = 0;
#else
                    argbdata[k++] = 0;
                    argbdata[k++] = pixels[i * rs + j]; // R
                    argbdata[k++] = pixels[i * rs + j + 1]; // G
                    argbdata[k++] = pixels[i * rs + j + 2]; // B
#endif
                }
            }
        }

#ifdef SUPPORT_LCMS
        if ((icc_profile = get_icc_profile(image_name)))
        {
            h_emb_profile = cmsOpenProfileFromMem(icc_profile + sizeof(cmsUInt32Number),
                                                  *(cmsUInt32Number *)icc_profile);
            if (h_display_profile == NULL)
            {
                h_display_profile = cmsCreate_sRGBProfile();
            }

            h_emb_transform = cmsCreateTransform(h_emb_profile,
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                                                 TYPE_BGRA_8, h_display_profile, TYPE_BGRA_8,
#else
                                                 TYPE_ARGB_8, h_display_profile, TYPE_ARGB_8,
#endif
                                                 INTENT_PERCEPTUAL, 0);
            if (h_emb_transform)
            {
                cmsDoTransform(h_emb_transform, argbdata, argbdata, pb_w * pb_h);
                cmsCloseProfile(h_emb_profile);
                cmsDeleteTransform(h_emb_transform);
            }
            else
            {
                printf("qiv warning: %s contains corrupt color profile\n", image_name);
            }
            free(icc_profile);
        }

        /* do the color transform */
        else if (cms_transform && h_cms_transform)
        {
            cmsDoTransform(h_cms_transform, argbdata, argbdata, pb_w * pb_h);
        }
#endif
        im = imlib_create_image_using_copied_data(pb_w, pb_h, (DATA32 *)argbdata);
        free(argbdata);
        if (*has_alpha)
        {
            g_object_unref(pixbuf);
        }
        g_object_unref(pixbuf_ori);
    }
    return im;
}

/*
 *    Load & display image
 */

void qiv_load_image(qiv_image *q)
{
    struct stat statbuf;
    const char *image_name = image_names[image_idx];
    Imlib_Image *im = NULL;
    int has_alpha = 0, rot;

    q->exposed = 0;
    gettimeofday(&load_before, 0);

    if (imlib_context_get_image())
        imlib_free_image();

    stat(image_name, &statbuf);
    current_mtime = statbuf.st_mtime;
    file_size = statbuf.st_size;

#ifdef DEBUG
    g_print("loading %s\n", image_name);
#endif

    /* use gdk_pixbuf for loading
    im = imlib_load_image( (char*)image_name );
    */

    im = im_from_pixbuf_loader((char *)image_name, &has_alpha);

    if (!im)
    { /* error */
        q->error = 1;
        q->orig_w = 400;
        q->orig_h = 300;
    }
    else
    { /* Retrieve image properties */
        imlib_context_set_image(im);
        if (has_alpha)
        {
            imlib_image_set_has_alpha(has_alpha);
        }
        q->error = 0;
        q->orig_w = imlib_image_get_width();
        q->orig_h = imlib_image_get_height();

        if (rotation > 10)
        {
            /* conditional rotation -- apply rotation only if image fits better */
            int screen_is_wide = monitor[q->mon_id].width > monitor[q->mon_id].height;
            int image_is_wide = q->orig_w > q->orig_h;
            int does_not_fit =
                q->orig_w > monitor[q->mon_id].width || q->orig_h > monitor[q->mon_id].height;
            if (screen_is_wide != image_is_wide && does_not_fit)
                rot = rotation - 10; /* we want the rotation (will be 11 -> 1 or 13 -> 3) */
            else
                rot = 0; /* don't rotate */
        }
        else
            rot = rotation;

        if (rot)
        {
            imlib_image_orientate(rot);
            if (rot != 2)
            {
                swap(&q->orig_w, &q->orig_h);
                swap(&q->win_w, &q->win_h);
            }
        }

        if (rot && rot != 2)
            correct_image_position(q);
    }

    if (first)
    {
        setup_win(q);
    }

    check_size(q, TRUE);

    /* desktop-background -> exit */
    if (to_root || to_root_t || to_root_s)
    {
        if (!im)
        {
            fprintf(stderr, "qiv: cannot load background_image\n");
            qiv_exit(1);
        }
        set_desktop_image(q);
        qiv_exit(0);
    }

    gdk_window_set_background(q->win, im ? &image_bg : &error_bg);

    gettimeofday(&load_after, 0);
    load_elapsed = ((load_after.tv_sec + load_after.tv_usec / 1.0e6) -
                    (load_before.tv_sec + load_before.tv_usec / 1.0e6));

    update_image(q, FULL_REDRAW);
    //    if (magnify && !fullscreen) {  // [lc]
    //     setup_magnify(q, &magnify_img);
    //     update_magnify(q, &magnify_img, FULL_REDRAW, 0, 0);
    //    }
}

static void setup_imlib_for_drawable(GdkDrawable *d)
{
    imlib_context_set_dither(1); /* dither for depths < 24bpp */
    imlib_context_set_display(gdk_x11_drawable_get_xdisplay(d));
    imlib_context_set_visual(gdk_x11_visual_get_xvisual(gdk_drawable_get_visual(d)));
    imlib_context_set_colormap(gdk_x11_colormap_get_xcolormap(gdk_drawable_get_colormap(d)));
    imlib_context_set_drawable(gdk_x11_drawable_get_xid(d));
}

static void setup_imlib_color_modifier(qiv_color_modifier q)
{
    if (q.gamma == DEFAULT_GAMMA && q.brightness == DEFAULT_BRIGHTNESS &&
        q.contrast == DEFAULT_CONTRAST)
    {
        if (imlib_context_get_color_modifier())
            imlib_free_color_modifier();
        return;
    }

    if (imlib_context_get_color_modifier())
        imlib_reset_color_modifier();
    else
        imlib_context_set_color_modifier(imlib_create_color_modifier());

    imlib_modify_color_modifier_gamma(q.gamma / 256.0);
    imlib_modify_color_modifier_brightness((q.brightness - 256) / 256.0);
    imlib_modify_color_modifier_contrast(q.contrast / 256.0);
}

static void setup_win(qiv_image *q)
{
    GdkWindowAttr attr;

    destroy_image(q);

    if (!fullscreen)
    {
        attr.window_type = GDK_WINDOW_TOPLEVEL;
        attr.wclass = GDK_INPUT_OUTPUT;
        attr.event_mask = GDK_ALL_EVENTS_MASK;
        attr.x = center ? q->win_x : 0;
        attr.y = center ? q->win_y : 0;
        attr.width = q->win_w;
        attr.height = q->win_h;
        q->win = gdk_window_new(NULL, &attr, GDK_WA_X | GDK_WA_Y);

        if (center)
        {
            GdkGeometry geometry = {.min_width = q->win_w,
                                    .min_height = q->win_h,
                                    .max_width = q->win_w,
                                    .max_height = q->win_h,
                                    .win_gravity = GDK_GRAVITY_STATIC};
            gdk_window_set_geometry_hints(
                q->win, &geometry, GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE | GDK_HINT_WIN_GRAVITY);
            gdk_window_move_resize(q->win, q->win_x, q->win_y, q->win_w, q->win_h);
        }
        else
        {
            GdkGeometry geometry = {
                .min_width = q->win_w,
                .min_height = q->win_h,
                .max_width = q->win_w,
                .max_height = q->win_h,
            };
            gdk_window_set_geometry_hints(q->win, &geometry, GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
            gdk_window_resize(q->win, q->win_w, q->win_h);
        }
        if (!(to_root || to_root_t || to_root_s))
            gdk_window_lower(q->win);
    }
    else
    { /* fullscreen */

        attr.window_type = GDK_WINDOW_TOPLEVEL;
        attr.wclass = GDK_INPUT_OUTPUT;
        attr.event_mask = GDK_ALL_EVENTS_MASK;
        //    attr.x = 0;
        //    attr.y = 0;
        attr.x = monitor[q->mon_id].x;
        attr.y = monitor[q->mon_id].y;
        //    printf("mon_id %d \n",q->mon_id);
        attr.width = monitor[q->mon_id].width;
        attr.height = monitor[q->mon_id].height;

        //    q->win = gdk_window_new(NULL, &attr, GDK_WA_X|GDK_WA_Y|GDK_WA_TYPE_HINT);
        q->win = gdk_window_new(NULL, &attr, GDK_WA_X | GDK_WA_Y);
        if (!(to_root || to_root_t || to_root_s))
        {
            gdk_window_fullscreen(q->win);
            gdk_window_show(q->win);
        }
    }

    q->bg_gc = gdk_gc_new(q->win);
    q->text_gc = gdk_gc_new(q->win); /* black is default */
    q->status_gc = gdk_gc_new(q->win);
    q->comment_gc = gdk_gc_new(q->win);
    gdk_gc_set_foreground(q->bg_gc, &image_bg);
    gdk_gc_set_foreground(q->status_gc, &text_bg);
    gdk_gc_set_foreground(q->comment_gc, &comment_bg);

    setup_imlib_for_drawable(GDK_DRAWABLE(q->win));
}

/* set image as background */

void set_desktop_image(qiv_image *q)
{
    GdkWindow *root_win = gdk_get_default_root_window();
    GdkVisual *gvis = gdk_drawable_get_visual(root_win);
    GdkPixmap *temp;
    gchar *buffer;

    gint root_w = screen_x, root_h = screen_y;
    gint root_x = 0, root_y = 0;

    Pixmap x_pixmap, x_mask;

    if (to_root || to_root_t)
    {
        root_w = q->win_w;
        root_h = q->win_h;
    }

    if (to_root)
    {
        root_x = (screen_x - root_w) / 2;
        root_y = (screen_y - root_h) / 2;
    }

    setup_imlib_for_drawable(GDK_DRAWABLE(root_win));

    imlib_render_pixmaps_for_whole_image_at_size(&x_pixmap, &x_mask, root_w, root_h);
#ifdef DEBUG
    if (x_mask)
        g_print("*** image has transparency\n");
#endif

    if (x_pixmap)
    {
        GdkPixmap *p = gdk_pixmap_foreign_new(x_pixmap);
        gdk_drawable_set_colormap(GDK_DRAWABLE(p),
                                  gdk_drawable_get_colormap(GDK_DRAWABLE(root_win)));
        if (to_root_t)
        {
            gdk_window_set_back_pixmap(root_win, p, FALSE);
        }
        else
        {
            GdkGC *rootGC;
            buffer = calloc(1, screen_x * screen_y * gdk_visual_get_depth(gvis) / 8);
            rootGC = gdk_gc_new(root_win);
            temp = gdk_pixmap_create_from_data(root_win, buffer, screen_x, screen_y,
                                               gdk_visual_get_depth(gvis), &image_bg, &image_bg);
            gdk_drawable_set_colormap(GDK_DRAWABLE(temp),
                                      gdk_drawable_get_colormap(GDK_DRAWABLE(root_win)));
            gdk_draw_drawable(temp, rootGC, p, 0, 0, root_x, root_y, root_w, root_h);
            gdk_window_set_back_pixmap(root_win, temp, FALSE);
            g_object_unref(temp);
            g_object_unref(rootGC);
            free(buffer);
        }
        g_object_unref(p);
        imlib_free_pixmap_and_mask(x_pixmap);
    }
    gdk_window_clear(root_win);
    gdk_flush();

    setup_imlib_for_drawable(q->win);
}

void zoom_in(qiv_image *q)
{
    int zoom_percentage;
    int w_old, h_old;

    /* first compute current zoom_factor */
    if (maxpect || scale_down || fixed_window_size)
    {
        zoom_percentage = myround((1.0 - (q->orig_w - q->win_w) / (double)q->orig_w) * 100);
        zoom_factor = (zoom_percentage - 100) / 10;
    }

    maxpect = scale_down = 0;

    zoom_factor++;
    w_old = q->win_w;
    h_old = q->win_h;
    q->win_w = (gint)(q->orig_w * (1 + zoom_factor * 0.1));
    q->win_h = (gint)(q->orig_h * (1 + zoom_factor * 0.1));

    /* adapt image position */
    q->win_x -= (q->win_w - w_old) / 2;
    q->win_y -= (q->win_h - h_old) / 2;

    if (fullscreen)
    {
        if (center)
            center_image(q);
        else
            correct_image_position(q);
    }
    else
    {
        correct_image_position(q);
    }
}

void zoom_out(qiv_image *q)
{
    int zoom_percentage;
    int w_old, h_old;

    /* first compute current zoom_factor */
    if (maxpect || scale_down || fixed_window_size)
    {
        zoom_percentage = myround((1.0 - (q->orig_w - q->win_w) / (double)q->orig_w) * 100);
        zoom_factor = (zoom_percentage - 100) / 10;
    }

    maxpect = scale_down = 0;

    w_old = q->win_w;
    h_old = q->win_h;

    if (zoom_factor > -9 && q->win_w > MIN(64, q->orig_w) && q->win_h > MIN(64, q->orig_h))
    {
        zoom_factor--;
        q->win_w = (gint)(q->orig_w * (1 + zoom_factor * 0.1));
        q->win_h = (gint)(q->orig_h * (1 + zoom_factor * 0.1));

        /* adapt image position */
        q->win_x -= (q->win_w - w_old) / 2;
        q->win_y -= (q->win_h - h_old) / 2;

        if (fullscreen)
        {
            if (center)
                center_image(q);
            else
                correct_image_position(q);
        }
        else
        {
            correct_image_position(q);
        }
    }
    else
    {
        snprintf(infotext, sizeof infotext, "(Cannot zoom out anymore)");
        fprintf(stderr, "qiv: cannot zoom out anymore\n");
    }
}

void zoom_maxpect(qiv_image *q)
{
    double zx = (double)monitor[q->mon_id].width / (double)q->orig_w;
    double zy = (double)monitor[q->mon_id].height / (double)q->orig_h;

    /* titlebar and frames ignored on purpose to use full height/width of screen */
    q->win_w = (gint)(q->orig_w * MIN(zx, zy));
    q->win_h = (gint)(q->orig_h * MIN(zx, zy));
    center_image(q);
}

/*
  Set display settings to startup values
  which are used whenever a new image is loaded.
*/

void reload_image(qiv_image *q)
{
    Imlib_Image *im;
    int has_alpha = 0;

    imlib_image_set_changes_on_disk();

    im = im_from_pixbuf_loader(image_names[image_idx], &has_alpha);

    if (!im && watch_file)
        return;

    struct stat statbuf;
    stat(image_names[image_idx], &statbuf);
    current_mtime = statbuf.st_mtime;

    if (imlib_context_get_image())
        imlib_free_image();

    if (!im)
    {
        q->error = 1;
        q->orig_w = 400;
        q->orig_h = 300;
    }
    else
    { /* Retrieve image properties */
        q->error = 0;
        imlib_context_set_image(im);
        if (has_alpha)
        {
            imlib_image_set_has_alpha(has_alpha);
        }
        q->orig_w = imlib_image_get_width();
        q->orig_h = imlib_image_get_height();
    }

    q->win_w = (gint)(q->orig_w * (1 + zoom_factor * 0.1));
    q->win_h = (gint)(q->orig_h * (1 + zoom_factor * 0.1));
    reset_mod(q);
    if (center)
        center_image(q);
}

void check_size(qiv_image *q, gint reset)
{
    if (maxpect || (scale_down && (q->orig_w > monitor[q->mon_id].width ||
                                   q->orig_h > monitor[q->mon_id].height)))
    {
        zoom_maxpect(q);
    }
    else if (reset || (scale_down && (q->win_w < q->orig_w || q->win_h < q->orig_h)))
    {
        reset_coords(q);
    }
    if (center)
    {
        center_image(q);
    }
    else if (fullscreen)
    {
        if (q->win_x > monitor[q->mon_id].x)
            q->win_x -= monitor[q->mon_id].x;
        if (q->win_y > monitor[q->mon_id].y)
            q->win_y -= monitor[q->mon_id].y;
    }
    else
    {
        if (q->win_x < monitor[q->mon_id].x)
            q->win_x += monitor[q->mon_id].x;
        if (q->win_y < monitor[q->mon_id].y)
            q->win_y += monitor[q->mon_id].y;
    }
}

void reset_coords(qiv_image *q)
{
    if (fixed_window_size)
    {
        double w_o_ratio = (double)(fixed_window_size) / q->orig_w;
        q->win_w = fixed_window_size;
        q->win_h = q->orig_h * w_o_ratio;
    }
    else
    {
        q->win_w = (gint)(q->orig_w * (1 + zoom_factor * 0.1));
        q->win_h = (gint)(q->orig_h * (1 + zoom_factor * 0.1));
    }
}

/* Something changed the image.  Redraw it. */

void update_image(qiv_image *q, int mode)
{
    static GdkPixmap *m = NULL;
    Pixmap x_pixmap, x_mask;
    double elapsed = 0;
    struct timeval before, after;
    int i;

    if (q->error)
    {
        g_snprintf(q->win_title, sizeof q->win_title, "qiv: ERROR! cannot load image: %s",
                   image_names[image_idx]);
        gdk_beep();

        /* take this image out of the file list */
        --images;
        for (i = image_idx; i < images; ++i)
        {
            image_names[i] = image_names[i + 1];
        }

        /* If deleting the last file out of x */
        if (images == image_idx)
            image_idx = 0;

        /* If deleting the only file left */
        if (!images)
        {
#ifdef DEBUG
            g_print("*** deleted last file in list. Exiting.\n");
#endif
            exit(0);
        }
        /* else load the next image */
        qiv_load_image(q);
        return;
    }
    else
    {
        if (mode == REDRAW || mode == FULL_REDRAW)
            setup_imlib_color_modifier(q->mod);

        if (mode == MOVED)
        {
            if (transparency && used_masks_before)
            {
                /* there should be a faster way to update the mask, but how? */
                if (q->p)
                {
                    imlib_free_pixmap_and_mask(GDK_PIXMAP_XID(q->p));
                    g_object_unref(q->p);
                }
                if (m)
                    g_object_unref(m);
                imlib_render_pixmaps_for_whole_image_at_size(&x_pixmap, &x_mask, q->win_w,
                                                             q->win_h);
                q->p = gdk_pixmap_foreign_new(x_pixmap);
                gdk_drawable_set_colormap(GDK_DRAWABLE(q->p),
                                          gdk_drawable_get_colormap(GDK_DRAWABLE(q->win)));
                m = gdk_pixmap_foreign_new(x_mask);
            }

            g_snprintf(q->win_title, sizeof q->win_title,
                       "qiv: %s (%dx%d) %d%% [%d/%d] b%d/c%d/g%d %s", image_names[image_idx],
                       q->orig_w, q->orig_h,
                       myround((1.0 - (q->orig_w - q->win_w) / (double)q->orig_w) * 100),
                       image_idx + 1, images, q->mod.brightness / 8 - 32, q->mod.contrast / 8 - 32,
                       q->mod.gamma / 8 - 32, infotext);
            snprintf(infotext, sizeof infotext, "(-)");

        } // mode == MOVED
        else
        {
            if (mode != MIN_REDRAW)
            {
                GdkPixmap *pix_ptr = NULL;
                if (q->p)
                {
                    imlib_free_pixmap_and_mask(GDK_PIXMAP_XID(q->p));
                    g_object_unref(q->p);
                }
                if (m)
                    g_object_unref(m);

                /* calculate elapsed time while we render image */
                gettimeofday(&before, 0);
                imlib_render_pixmaps_for_whole_image_at_size(&x_pixmap, &x_mask, q->win_w,
                                                             q->win_h);
                gettimeofday(&after, 0);
                elapsed = ((after.tv_sec + after.tv_usec / 1.0e6) -
                           (before.tv_sec + before.tv_usec / 1.0e6));

                pix_ptr = gdk_pixmap_lookup(x_pixmap);
                if (pix_ptr == NULL)
                {
                    q->p =
                        gdk_pixmap_foreign_new_for_screen(screen, x_pixmap, q->win_w, q->win_h, 24);
                }
                else
                {
                    q->p = pix_ptr;
                    g_object_ref(q->p);
                }
                gdk_drawable_set_colormap(GDK_DRAWABLE(q->p),
                                          gdk_drawable_get_colormap(GDK_DRAWABLE(q->win)));
                m = x_mask == None
                        ? NULL
                        : gdk_pixmap_foreign_new_for_screen(screen, x_mask, q->win_w, q->win_h, 1);
            }

#ifdef DEBUG
            if (m)
                g_print("*** image has transparency\n");
#endif

            g_snprintf(q->win_title, sizeof q->win_title,
                       "qiv: %s (%dx%d) %1.01fs %d%% [%d/%d] b%d/c%d/g%d %s",
                       image_names[image_idx], q->orig_w, q->orig_h, load_elapsed + elapsed,
                       myround((1.0 - (q->orig_w - q->win_w) / (double)q->orig_w) * 100),
                       image_idx + 1, images, q->mod.brightness / 8 - 32, q->mod.contrast / 8 - 32,
                       q->mod.gamma / 8 - 32, infotext);
            snprintf(infotext, sizeof infotext, "(-)");
        }
    }

    gdk_window_set_title(q->win, q->win_title);

    q->text_len = strlen(q->win_title);
    pango_layout_set_text(layout, q->win_title, -1);
    pango_layout_get_pixel_size(layout, &(q->text_w), &(q->text_h));

    if (comment && comment_window)
    {
        pango_layout_set_text(layoutComment, comment, -1);
        pango_layout_get_pixel_size(layoutComment, &(q->comment_w), &(q->comment_h));
    }

    if (!fullscreen)
    {
        GdkGeometry geometry = {.min_width = q->win_w,
                                .min_height = q->win_h,
                                .max_width = q->win_w,
                                .max_height = q->win_h,
                                .win_gravity = GDK_GRAVITY_STATIC};
        gdk_window_set_geometry_hints(q->win, &geometry,
                                      GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE | GDK_HINT_WIN_GRAVITY);

        if (first)
        {
            gdk_window_show(q->win);
            first = 0;
            q->exposed = 0;
        }

        if (mode != MIN_REDRAW)
            gdk_window_move_resize(q->win, q->win_x, q->win_y, q->win_w, q->win_h);

        if (!q->error)
        {
            gdk_window_set_back_pixmap(q->win, q->p, FALSE);
            /* remove or set transparency mask */
            if (used_masks_before)
            {
                if (transparency)
                    gdk_window_shape_combine_mask(q->win, m, 0, 0);
                else
                    gdk_window_shape_combine_mask(q->win, 0, 0, 0);
            }
            else
            {
                if (transparency && m)
                {
                    gdk_window_shape_combine_mask(q->win, m, 0, 0);
                    used_masks_before = 1;
                }
            }
        }
        gdk_window_clear(q->win);

        if (statusbar_window)
        {
#ifdef DEBUG
            g_print("*** print statusbar at (%d, %d)\n", MAX(2, q->win_w - q->text_w - 10),
                    MAX(2, q->win_h - q->text_h - 10));
#endif
            gdk_draw_rectangle(q->win, q->bg_gc, 0, MAX(2, q->win_w - q->text_w - 10),
                               MAX(2, q->win_h - q->text_h - 10), q->text_w + 5, q->text_h + 5);
            gdk_draw_rectangle(q->win, q->status_gc, 1, MAX(3, q->win_w - q->text_w - 9),
                               MAX(3, q->win_h - q->text_h - 9), q->text_w + 4, q->text_h + 4);

            gdk_draw_layout(q->win, q->text_gc, MAX(5, q->win_w - q->text_w - 7),
                            MAX(5, q->win_h - 7 - q->text_h), layout);
        }

        if (comment && comment_window)
        {
            /* draw comment */
            gdk_draw_rectangle(q->win, q->bg_gc, 0, 25, MAX(5, q->win_h - q->comment_h - 30),
                               q->comment_w + 5, q->comment_h + 5);

            gdk_draw_rectangle(q->win, q->comment_gc, 1, 26, MAX(6, q->win_h - q->comment_h - 29),
                               q->comment_w + 4, q->comment_h + 4);

            gdk_draw_layout(q->win, q->text_gc, 27, MAX(5, q->win_h - 27 - q->comment_h),
                            layoutComment);
        }

    } // if (!fullscreen)
    else
    {
#define statusbar_x monitor[q->mon_id].width
#define statusbar_y monitor[q->mon_id].height
        if (mode == FULL_REDRAW)
        {
            gdk_window_clear(q->win);
        }
        else
        {
            if (q->win_x > q->win_ox)
                gdk_draw_rectangle(q->win, q->bg_gc, 1, q->win_ox, q->win_oy, q->win_x - q->win_ox,
                                   q->win_oh);
            if (q->win_y > q->win_oy)
                gdk_draw_rectangle(q->win, q->bg_gc, 1, q->win_ox, q->win_oy, q->win_ow,
                                   q->win_y - q->win_oy);
            if (q->win_x + q->win_w < q->win_ox + q->win_ow)
                gdk_draw_rectangle(q->win, q->bg_gc, 1, q->win_x + q->win_w, q->win_oy,
                                   q->win_ox + q->win_ow, q->win_oh);
            if (q->win_y + q->win_h < q->win_oy + q->win_oh)
                gdk_draw_rectangle(q->win, q->bg_gc, 1, q->win_ox, q->win_y + q->win_h, q->win_ow,
                                   q->win_oy + q->win_oh);

            if (q->statusbar_was_on &&
                (!statusbar_fullscreen || q->text_ow > q->text_w || q->text_oh > q->text_h))
                gdk_draw_rectangle(q->win, q->bg_gc, 1, statusbar_x - q->text_ow - 9,
                                   statusbar_y - q->text_oh - 9, q->text_ow + 4, q->text_oh + 4);
        }

        /* remove or set transparency mask */
        if (used_masks_before)
        {
            if (transparency)
                gdk_window_shape_combine_mask(q->win, m, q->win_x, q->win_y);
            else
                gdk_window_shape_combine_mask(q->win, 0, q->win_x, q->win_y);
        }
        else
        {
            if (transparency && m)
            {
                gdk_window_shape_combine_mask(q->win, m, q->win_x, q->win_y);
                used_masks_before = 1;
            }
        }

        if (!q->error)
            gdk_draw_drawable(q->win, q->bg_gc, q->p, 0, 0, q->win_x, q->win_y, q->win_w, q->win_h);

        if (statusbar_fullscreen)
        {
            gdk_draw_rectangle(q->win, q->bg_gc, 0, statusbar_x - q->text_w - 10,
                               statusbar_y - q->text_h - 10, q->text_w + 5, q->text_h + 5);

            gdk_draw_rectangle(q->win, q->status_gc, 1, statusbar_x - q->text_w - 9,
                               statusbar_y - q->text_h - 9, q->text_w + 4, q->text_h + 4);

            gdk_draw_layout(q->win, q->text_gc, statusbar_x - q->text_w - 7,
                            statusbar_y - 7 - q->text_h, layout);
        }

        if (comment && comment_window)
        {
            /* draw comment */
            gdk_draw_rectangle(q->win, q->bg_gc, 0, 25, statusbar_y - q->comment_h - 30,
                               q->comment_w + 5, q->comment_h + 5);

            gdk_draw_rectangle(q->win, q->comment_gc, 1, 26, statusbar_y - q->comment_h - 29,
                               q->comment_w + 4, q->comment_h + 4);

            gdk_draw_layout(q->win, q->text_gc, 27, statusbar_y - 27 - q->comment_h, layoutComment);
        }

        q->win_ox = q->win_x;
        q->win_oy = q->win_y;
        q->win_ow = q->win_w;
        q->win_oh = q->win_h;
        q->text_ow = q->text_w;
        q->text_oh = q->text_h;
        q->statusbar_was_on = statusbar_fullscreen;

        if (first)
        {
            gdk_window_show(q->win);
            first = 0;
        }

        gdk_window_move_resize(q->win, monitor[q->mon_id].x, monitor[q->mon_id].y,
                               monitor[q->mon_id].width, monitor[q->mon_id].height);
    }
    gdk_flush();
}

void reset_mod(qiv_image *q)
{
    q->mod.brightness = default_brightness;
    q->mod.contrast = default_contrast;
    q->mod.gamma = default_gamma;
}

void destroy_image(qiv_image *q)
{
    if (q->p)
    {
        imlib_free_pixmap_and_mask(GDK_PIXMAP_XID(q->p));
        g_object_unref(q->p);
    }
    if (q->win)
        g_object_unref(q->win);
    if (q->bg_gc)
        g_object_unref(q->bg_gc);
    if (q->text_gc)
        g_object_unref(q->text_gc);
    if (q->status_gc)
        g_object_unref(q->status_gc);
    if (q->comment_gc)
        g_object_unref(q->comment_gc);

    q->p = NULL;
    q->win = NULL;
    q->bg_gc = NULL;
    q->text_gc = NULL;
    q->status_gc = NULL;
    q->comment_gc = NULL;
}

void setup_magnify(qiv_image *q, qiv_mgl *m)
{
    GdkWindowAttr mgl_attr;
    GdkGeometry mgl_hints;

    m->win_w = 300;
    m->win_h = 200;
    m->zoom = 2.0;

    //   gdk_flush();
    gdk_window_get_root_origin(q->win, &m->frame_x, &m->frame_y);
    // printf("frame %d %d\n", m->frame_x, m->frame_y);

    mgl_attr.window_type = GDK_WINDOW_TOPLEVEL; // Set up attributes for GDK to create a Window
    mgl_attr.wclass = GDK_INPUT_OUTPUT;
    mgl_attr.event_mask = GDK_STRUCTURE_MASK | GDK_EXPOSURE_MASK;
    mgl_attr.width = m->win_w;
    mgl_attr.height = m->win_h;
    mgl_attr.override_redirect = TRUE;
    //   m->win=gdk_window_new(NULL,&mgl_attr,GDK_WA_X|GDK_WA_Y|GDK_WA_WMCLASS);
    m->win = gdk_window_new(NULL, &mgl_attr, GDK_WA_X | GDK_WA_Y);
    mgl_hints.min_width = m->win_w;
    mgl_hints.max_width = m->win_w;
    mgl_hints.min_height = m->win_h;
    mgl_hints.max_height = m->win_h;
    gdk_window_set_geometry_hints(m->win, &mgl_hints, GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
    gdk_window_set_decorations(m->win, GDK_DECOR_BORDER);
    gdk_flush();
}

void update_magnify(qiv_image *q, qiv_mgl *m, int mode, gint xcur, gint ycur)
{
    //   GdkWindowAttr mgl_attr;
    //   GdkGeometry mgl_hints;
    register gint xx, yy; // win_pos_x, win_pos_y;

    /***********
      if (mode == FULL_REDRAW) {
    //    printf("> update_magnify: FULL_REDRAW \n");
        m->im=gdk_imlib_crop_and_clone_image(
          q->im, 0, 0, m->win_w, m->win_h);
        gdk_imlib_apply_image(m->im,m->win);

        mgl_hints.min_width=m->win_w;
        mgl_hints.max_width=m->win_w;
        mgl_hints.min_height=m->win_h;
        mgl_hints.max_height=m->win_h;

    //    gdk_window_set_hints(m->win, mgl_attr.x, mgl_attr.y,mglw,
    //                         mglw,mglh, mglh, GDK_HINT_POS | GDK_HINT_MIN_SIZE |
    GDK_HINT_MAX_SIZE); gdk_window_set_hints(m->win, xcur+50, ycur-50-m->win_h,m->win_w,
                             m->win_w,m->win_h, m->win_h, GDK_HINT_POS | GDK_HINT_MIN_SIZE |
    GDK_HINT_MAX_SIZE);
    //    gdk_window_move_resize(m->win,mgl_attr.x, mgl_attr.y, mglw, mglh);

    //    gdk_window_set_geometry_hints(magnify_img.win, &mgl_hints, GDK_HINT_POS );
      }
    ************/
    if (mode == REDRAW)
    {
        /* scale position to original size */
        xx = xcur * ((double)q->orig_w / (double)q->win_w);
        yy = ycur * ((double)q->orig_h / (double)q->win_h);

        /* center mouse cursor position */
        xx -= 150 / m->zoom;
        yy -= 100 / m->zoom;

        /* keep magnify part allways inside image */
        if (xx + m->win_w / m->zoom > q->orig_w)
        {
            xx = q->orig_w - m->win_w / m->zoom;
        }
        if (yy + m->win_h / m->zoom > q->orig_h)
        {
            yy = q->orig_h - m->win_h / m->zoom;
        }

        setup_imlib_for_drawable(m->win);
        imlib_render_image_part_on_drawable_at_size(xx, yy, m->win_w / m->zoom, m->win_h / m->zoom,
                                                    0, 0, m->win_w, m->win_h);
        setup_imlib_for_drawable(q->win);
        gdk_window_show(m->win);

        xx = m->frame_x + xcur - 50 - m->win_w;
        yy = m->frame_y + ycur - 50 - m->win_h;
        if (xx < 0)
        {
            if (xcur < m->win_w - magnify_img.frame_x)
                xx = m->frame_x + xcur + 50;
            else
                xx = 0;
        }
        if (yy < 0)
        {
            if (ycur < m->win_h - magnify_img.frame_y)
                yy = m->frame_y + ycur + 50;
            else
                yy = 0;
        }
        //    printf("MGL: m->frame_x: %d, m->frame_y: %d, xx: %d, yy: %d\n", m->frame_x,
        //    m->frame_y, xx, yy);
        gdk_window_move(m->win, xx, yy);
    }
    gdk_flush();
}

void center_image(qiv_image *q)
{
    q->win_x = (monitor[q->mon_id].width - q->win_w) / 2;
    q->win_y = (monitor[q->mon_id].height - q->win_h) / 2;
    if (!fullscreen)
    {
        q->win_x += monitor[q->mon_id].x;
        q->win_y += monitor[q->mon_id].y;
    }
}

void correct_image_position(qiv_image *q)
{
    //  g_print("before: q->win_x = %d, q->win_y = %d, q->win_w = %d\n", q->win_x, q->win_y,
    //  q->win_w);

    /* try to keep inside the screen */
    if (q->win_w < screen_x)
    {
        if (q->win_x < 0)
            q->win_x = 0;
        if (q->win_x + q->win_w > screen_x)
            q->win_x = screen_x - q->win_w;
    }
    else
    {
        if (q->win_x > 0)
            q->win_x = 0;
        if (q->win_x + q->win_w < screen_x)
            q->win_x = screen_x - q->win_w;
    }

    /* don't leave ugly borders */
    if (q->win_h < screen_y)
    {
        if (q->win_y < 0)
            q->win_y = 0;
        if (q->win_y + q->win_h > screen_y)
            q->win_y = screen_y - q->win_h;
    }
    else
    {
        if (q->win_y > 0)
            q->win_y = 0;
        if (q->win_y + q->win_h < screen_y)
            q->win_y = screen_y - q->win_h;
    }
    //  g_print("after:  q->win_x = %d, q->win_y = %d, q->win_w = %d\n", q->win_x, q->win_y,
    //  q->win_w);
}
