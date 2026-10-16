#include "race_ui.h"
#include "race_ui_data.h"
#include "gfx2d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ign_race_points(int place) {
    /* 0xc548c is indexed by one-based car+0x3a0; only slots 1..6 are a points table. */
    static const int points[] = {0, 10, 7, 5, 3, 2, 1};
    return place >= 1 && place <= 6 ? points[place] : 0;
}

int ign_champ_track(int klass, int round) {
    static const int tracks[4][7] = {
        {0, 1, 2, 3, 4, 0, 0}, {3, 2, 0, 4, 1, 5, 0},
        {4, 3, 5, 1, 0, 2, 6}, {5, 0, 4, 6, 2, 1, 3}
    };
    if (klass < 0 || klass > 3 || round < 0 || round > 6) {
        fprintf(stderr, "race UI: invalid championship class/round %d/%d\n", klass, round);
        return -1;
    }
    return tracks[klass][round];
}

void ign_race_score_order(const int *points, int n, int *order) {
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && points[order[j - 1]] < points[i]) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
}

double ign_frame_time_sample(IgnFrameTime *time, int32_t now, int paused) {
    double sample = (double)now * 0.036;
    double delta = sample - time->previous;
    if (delta < 1.0) return delta;
    time->previous = sample;
    time->fps = (int)(36.0 / delta);
    time->fps_sum += time->fps;
    time->frames++;
    if (paused) {
        double origin = (double)time->origin + delta * 27.77777777777778;
        time->origin = (int32_t)(uint32_t)(int64_t)origin;
    }
    int64_t difference = (int64_t)now - time->origin;
    int32_t wrapped = difference > INT32_MAX ? (int32_t)(difference - INT64_C(4294967296)) :
                      difference < INT32_MIN ? (int32_t)(difference + INT64_C(4294967296)) :
                      (int32_t)difference;
    time->elapsed = (double)wrapped * 0.001;
    return delta > 10.8 ? 10.8 : delta;
}

uint32_t ign_bios_ticks(uint64_t milliseconds) {
    return (uint32_t)((milliseconds % UINT64_C(86400000)) * UINT64_C(1573040) / UINT64_C(86400000));
}

static int load_font(IgnFont *font, const char *dir, const char *name) {
    char path[1200];
    int n = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof path || ign_font_load_lft(font, path)) {
        fprintf(stderr, "race UI: cannot load font %s\n", name);
        return -1;
    }
    font->proportional = 1;
    return 0;
}

/* 0x21d68: three box groups in a 320x480 scratch layer, composited into a 320x200 screen. */
static void loading_build(IgnRaceLoading *loading) {
    IgnFrame *frame = loading->frame;
    ign_frame_init(frame, 320, 200);
    memcpy(frame->color, loading->background.pixels, 320 * 200);
    const uint8_t *identity = ign_gfx_identity_lut();
    for (int i = 0; i < 4; i++) {
        IgnSprite corner;
        ign_sprite_from_pic(&corner, &loading->system, 236, i * 20, 20, 20);
        ign_sprite_draw(frame, NULL, &corner, i & 1 ? 300 : 0, i & 2 ? 180 : 0, loading->pan);
    }
    const char *title = race_text_track[loading->language][loading->track];
    const char *label = race_text_loading[loading->language][0];
    int title_width = ign_font_text_width(&loading->font, title);
    int label_width = ign_font_text_width(&loading->font, label);
    IgnMenuBox title_box = {106, 106 + title_width, 0, 18, 0, NULL};
    ign_menubox_draw_list(loading->overlay, 320, 320, 480, loading->boxes, &title_box);
    /* Glyph drawing uses the native frame type; save the background while drawing the opaque text layer. */
    uint8_t background[320 * 200];
    memcpy(background, frame->color, sizeof background);
    memcpy(frame->color, loading->overlay, sizeof background);
    ign_font_draw(frame, NULL, &loading->font, title, 119, 30, identity);
    memcpy(loading->overlay, frame->color, sizeof background);
    memcpy(frame->color, background, sizeof background);
    ign_blend_rect(loading->overlay, 0, 0, 320, 200, frame->color, 0, 0, loading->pan, 320, 320, 320, 200);

    memset(loading->overlay, 0, 320 * 480);
    IgnMenuBox lower = {106, 106 + title_width, 1, 45, 1, NULL};
    IgnMenuBox upper = {106, 106 + label_width, 0, 45, 1, &lower};
    ign_menubox_draw_list(loading->overlay, 320, 320, 480, loading->boxes, &upper);
    upper.yoff = lower.yoff = 95;
    upper.style = lower.style = 0;
    ign_menubox_draw_list(loading->overlay, 320, 320, 480, loading->boxes, &upper);
    memcpy(background, frame->color, sizeof background);
    memcpy(frame->color, loading->overlay, sizeof background);
    ign_font_draw(frame, NULL, &loading->blue, label, 119, 57, identity);
    ign_font_draw(frame, NULL, &loading->font, label, 119, 107, identity);
    memcpy(loading->overlay, frame->color, sizeof background);
    memcpy(frame->color, background, sizeof background);
    IgnSprite sprite;
    ign_sprite_from_pic(&sprite, &loading->system, 153, 190, 61, 58);
    ign_sprite_draw(frame, NULL, &sprite, 44, 1, loading->pan);
}

int ign_race_loading_init(IgnRaceLoading *loading, const char *data_dir, const char *level, int language) {
    memset(loading, 0, sizeof *loading);
    loading->language = language >= 0 && language < 6 ? language : 0;
    loading->track = ign_ghost_track_id(level);
    char path[1200];
    int n = snprintf(path, sizeof path, "%s/LEVELS/%s/%s.PIC", data_dir, level, level);
    if (loading->track < 0 || n < 0 || (size_t)n >= sizeof path ||
        ign_load_pic(path, &loading->background) ||
        loading->background.width != 320 || loading->background.height != 200 ||
        load_font(&loading->font, data_dir, "BALTAZAR/DATA/RED_LITE.LFT") ||
        load_font(&loading->blue, data_dir, "BALTAZAR/DATA/BLUELITE.LFT")) goto fail;
    n = snprintf(path, sizeof path, "%s/LEVELS/%s/%s.PAN", data_dir, level, level);
    size_t size;
    if (n < 0 || (size_t)n >= sizeof path || !(loading->pan = ign_read_file(path, &size)) || size != 65536)
        goto fail;
    n = snprintf(path, sizeof path, "%s/N_SYSGFX.PIC", data_dir);
    if (n < 0 || (size_t)n >= sizeof path || ign_load_pic(path, &loading->system) ||
        loading->system.width != 256 || loading->system.height < 248) goto fail;
    n = snprintf(path, sizeof path, "%s/MENUBKG.DAT", data_dir);
    if (n < 0 || (size_t)n >= sizeof path || !(loading->boxes = ign_read_file(path, &size)) || size < 0x9358)
        goto fail;
    loading->overlay = calloc(320 * 480, 1);
    loading->frame = malloc(sizeof *loading->frame);
    if (!loading->overlay || !loading->frame) goto fail;
    loading_build(loading);
    return 0;
fail:
    fprintf(stderr, "race UI: cannot build loading screen for %s (resource %s)\n", level, path);
    ign_race_loading_destroy(loading);
    return -1;
}

void ign_race_loading_destroy(IgnRaceLoading *loading) {
    ign_free_pic(&loading->background);
    ign_free_pic(&loading->system);
    ign_font_free(&loading->font);
    ign_font_free(&loading->blue);
    free(loading->pan);
    free(loading->boxes);
    free(loading->overlay);
    free(loading->frame);
    memset(loading, 0, sizeof *loading);
}

int ign_race_loading_draw(IgnRaceLoading *loading, IgnFrame *frame, int percent) {
    if (percent < 0 || percent > 100 || !loading->background.pixels || !loading->pan ||
        !loading->frame || !loading->overlay) {
        fprintf(stderr, "race UI: invalid loading progress %d or uninitialised resources\n", percent);
        return -1;
    }
    IgnFrame *screen = loading->frame;
    /* 0x2225c: restore x=104..294, y=0..26 from the unadorned PIC, not the whole screen. */
    for (int y = 0; y < 27; y++)
        memcpy(screen->color + y * 320 + 104, loading->background.pixels + y * 320 + 104, 191);
    const char *label = race_text_loading[loading->language][0];
    /* 0x222aa..0x222e8: trunc((text_width + 20) * percent * .01 * 1.19). */
    int width = (int)((double)((ign_font_text_width(&loading->font, label) + 20) * percent) * 0.01 * 1.19);
    int split = 106 + width;
    ign_blend_rect(loading->overlay, 0, 50, split, 75, screen->color, 0, 4, loading->pan, 320, 320, 320, 200);
    ign_blend_rect(loading->overlay, split, 100, 320, 125, screen->color, split, 4,
                   loading->pan, 320, 320, 320, 200);
    for (int y = 0; y < frame->h; y++)
        for (int x = 0; x < frame->w; x++)
            frame->color[y * frame->w + x] = screen->color[(y * 200 / frame->h) * 320 + x * 320 / frame->w];
    return 0;
}

int ign_race_ui_init(IgnRaceUi *ui, const char *data_dir, const IgnBtz *btz, int mode, int ncars,
                     const int previous_points[8]) {
    memset(ui, 0, sizeof *ui);
    if (ncars < 1 || ncars > 8 || mode < 0 || mode > 3) {
        fprintf(stderr, "race UI: invalid mode or participant count\n");
        return -1;
    }
    ui->mode = mode;
    ui->ncars = ncars;
    ui->retries = 3;
    ui->language = btz ? ign_btz_i32(btz, IGN_BTZ_LANGUAGE) : 0;
    if (ui->language < 0 || ui->language > 5) ui->language = 0;
    ui->round = btz ? ign_btz_i32(btz, IGN_BTZ_TRACK) : 0;
    ui->klass = btz ? ign_btz_i32(btz, IGN_BTZ_CLASS) : 0;
    if (previous_points) memcpy(ui->total, previous_points, sizeof ui->total);
    char path[1200];
    int n;
    if (load_font(&ui->normal, data_dir, "BALTAZAR/DATA/RED_DARK.LFT") ||
        load_font(&ui->selected, data_dir, "BALTAZAR/DATA/RED_LITE.LFT") ||
        load_font(&ui->small, data_dir, "FONTS/MINI.LFT")) goto fail;
    n = snprintf(path, sizeof path, "%s/POKAL.PIC", data_dir);
    if (n < 0 || (size_t)n >= sizeof path || ign_load_pic(path, &ui->trophy)) goto fail;
    return 0;
fail:
    fprintf(stderr, "race UI: cannot load system resources from %s\n", data_dir);
    ign_race_ui_destroy(ui);
    return -1;
}

void ign_race_ui_destroy(IgnRaceUi *ui) {
    ign_font_free(&ui->normal);
    ign_font_free(&ui->selected);
    ign_font_free(&ui->small);
    ign_free_pic(&ui->trophy);
}

void ign_race_ui_update(IgnRaceUi *ui, const IgnCar *const *cars) {
    int finished = 0;
    for (int i = 0; i < ui->ncars; i++) {
        ui->points[i] = cars[i]->out ? ign_race_points(cars[i]->place) : 0;
        finished += cars[i]->out != 0;
    }
    int places[8];
    for (int i = 0; i < ui->ncars; i++)
        places[i] = cars[i]->out && cars[i]->place > 0 ? -cars[i]->place : -100 - i;
    ign_race_score_order(places, ui->ncars, ui->order);
    /* 0x44030: results may open once all humans are out, while the AI keeps racing. */
    if (cars[0]->out && ui->page == IGN_RACE_UI_PLAY) ui->page = IGN_RACE_UI_RESULTS;
    if (finished == ui->ncars && !ui->committed) {
        for (int i = 0; i < ui->ncars; i++) ui->total[i] += ui->points[i];
        ui->committed = 1;
        ui->qualified = cars[0]->place >= 1 && cars[0]->place < 4;
        ui->complete = ign_champ_class_complete(ui->round, ui->klass);
    }
    int totals[8];
    for (int i = 0; i < ui->ncars; i++) totals[i] = ui->total[i] + (ui->committed ? 0 : ui->points[i]);
    ign_race_score_order(totals, ui->ncars, ui->standings);
}

int ign_race_ui_key(IgnRaceUi *ui, int key, int *music_delta) {
    *music_delta = 0;
    if (ui->page == IGN_RACE_UI_PLAY) {
        if (key == IGN_UI_ESCAPE) ui->page = IGN_RACE_UI_PAUSE, ui->selection = 0;
    } else if (ui->page == IGN_RACE_UI_QUIT || ui->page == IGN_RACE_UI_RESTART) {
        if (key == IGN_UI_YES && (ui->page != IGN_RACE_UI_RESTART || ui->mode != 0 || ui->retries > 0)) {
            ui->action = ui->page == IGN_RACE_UI_QUIT ? IGN_FLOW_MENU : IGN_FLOW_RESTART;
        } else if (key == IGN_UI_NO || key == IGN_UI_ESCAPE) ui->page = ui->return_page;
    } else if (ui->page == IGN_RACE_UI_PAUSE) {
        if (key == IGN_UI_ESCAPE) ui->page = IGN_RACE_UI_PLAY;
        else if (key == IGN_UI_UP) ui->selection = (ui->selection + 3) % 4;
        else if (key == IGN_UI_DOWN) ui->selection = (ui->selection + 1) % 4;
        else if (ui->selection == 3 && (key == IGN_UI_LEFT || key == IGN_UI_RIGHT))
            *music_delta = key == IGN_UI_LEFT ? -1 : 1;
        else if (key == IGN_UI_ENTER) {
            if (ui->selection == 0) ui->page = IGN_RACE_UI_PLAY;
            else if (ui->selection == 1 || ui->selection == 2) {
                ui->return_page = IGN_RACE_UI_PAUSE;
                ui->page = ui->selection == 1 ? IGN_RACE_UI_RESTART : IGN_RACE_UI_QUIT;
            }
        }
    } else if (key == IGN_UI_ESCAPE) {
        ui->return_page = ui->page;
        ui->page = IGN_RACE_UI_QUIT;
    } else if (ui->mode != 0 && ui->page == IGN_RACE_UI_RESULTS) {
        if (key == IGN_UI_YES) ui->action = IGN_FLOW_RESTART;
        else if (key == IGN_UI_NO || key == IGN_UI_ENTER) ui->action = IGN_FLOW_MENU;
    } else if (ui->page == IGN_RACE_UI_PODIUM && !ui->qualified && key == IGN_UI_UP) {
        ui->selection = 0;
    } else if (ui->page == IGN_RACE_UI_PODIUM && !ui->qualified && key == IGN_UI_DOWN) {
        ui->selection = 1;
    } else if (key == IGN_UI_ENTER && ui->committed) {
        if (ui->page == IGN_RACE_UI_RESULTS) ui->page = IGN_RACE_UI_TRACK_SCORE;
        else if (ui->page == IGN_RACE_UI_TRACK_SCORE) ui->page = IGN_RACE_UI_TOTAL_SCORE;
        else if (ui->page == IGN_RACE_UI_TOTAL_SCORE) ui->page = IGN_RACE_UI_PODIUM;
        else if (ui->page == IGN_RACE_UI_PODIUM) {
            if (!ui->qualified && ui->selection == 0 && ui->retries > 0) ui->action = IGN_FLOW_RESTART;
            else ui->action = ui->qualified && !ui->complete ? IGN_FLOW_ADVANCE : IGN_FLOW_MENU;
        }
    }
    return ui->action;
}

int ign_race_ui_notify(IgnRaceUi *ui, IgnHud *hud, int status, int car, const char *name, IgnCar *participant) {
    unsigned bit;
    const char *text;
    char message[100];
    if (status == 100 || status == 101) {
        bit = 1u << (status - 100);
        text = race_text_network[ui->language][status - 99];
    } else if (status == 102 && car >= 0 && car < ui->ncars && participant && name) {
        bit = 1u << (car + 2);
        snprintf(message, sizeof message, race_text_network[ui->language][0], name);
        text = message;
    } else {
        fprintf(stderr, "race UI: invalid notification %d for participant %d\n", status, car);
        return -1;
    }
    if (ui->notifications & bit) return 0;
    int slot = ign_hud_message(hud, text, 80, 8, -1);
    if (slot < 0) {
        fprintf(stderr, "race UI: message queue full\n");
        return -1;
    }
    ui->notifications |= bit;
    if (status == 102) participant->out = 1, participant->progress_lock = 0;
    return 1;
}

static void text(IgnFrame *frame, const IgnFont *font, const char *value, int x, int y, int centre,
                 const uint8_t *lut) {
    IgnFont copy = *font;
    copy.align = centre;
    ign_font_draw(frame, NULL, &copy, value, x, y, lut);
}

void ign_race_ui_draw(IgnRaceUi *ui, IgnFrame *frame, const IgnHud *hud, const IgnCar *const *cars,
                      const IgnBtz *btz, int music_track) {
    if (ui->page == IGN_RACE_UI_PLAY) return;
    const uint8_t *lut = hud->pan;
    int lang = ui->language;
    int left = (frame->w - 320) / 2, top = frame->h / 2 - 50;
    for (int y = 0; y < frame->h; y++)
        for (int x = 0; x < frame->w; x++)
            frame->color[y * frame->w + x] = lut ? lut[0x800 | frame->color[y * frame->w + x]] : 0;
    if (ui->page <= IGN_RACE_UI_RESTART) {
        if (ui->page == IGN_RACE_UI_PAUSE) {
            for (int i = 0; i < 4; i++)
                text(frame, i == ui->selection ? &ui->selected : &ui->normal,
                     race_text_pause[lang][i], left + 70, top + 12 + i * 19, 0, lut);
            char music[32];
            snprintf(music, sizeof music, "%d", music_track);
            text(frame, &ui->small, music, left + 235, top + 69, 0, lut);
        } else {
            text(frame, &ui->selected, ui->page == IGN_RACE_UI_QUIT ? race_text_quit[lang][0] :
                 race_text_restart[lang][0], frame->w / 2, frame->h / 2 - 3, 1, lut);
        }
        return;
    }
    /* Native reflow: all eight port participants fit in 320x200; the DOS uses clipped strips of a 320x480 page. */
    const char *title = ui->page == IGN_RACE_UI_TRACK_SCORE ? race_text_track_score[lang][0] :
                        ui->page == IGN_RACE_UI_TOTAL_SCORE ? race_text_total_score[lang][0] :
                        race_text_results[lang][0];
    text(frame, &ui->selected, title, frame->w / 2, 10, 1, lut);
    if (ui->page == IGN_RACE_UI_PODIUM) {
        const char *message = !ui->qualified ? race_text_failed[lang][0] :
                              ui->complete ? race_text_complete[lang][0] : race_text_advance[lang][0];
        if (ui->qualified && ui->trophy.pixels) {
            IgnSprite trophy = {ui->trophy.pixels, ui->trophy.width, ui->trophy.height, ui->trophy.width};
            ign_sprite_draw(frame, NULL, &trophy, frame->w / 2 - trophy.w / 2,
                            frame->h / 2 - trophy.h / 2, lut);
        }
        text(frame, &ui->small, message, frame->w / 2, frame->h - 32, 1, lut);
        if (!ui->qualified)
            text(frame, &ui->small, race_text_failed[lang][1], frame->w / 2, frame->h - 20, 1, lut);
    } else {
        int step = (frame->h - 70) / ui->ncars;
        if (step > 25) step = 25;
        for (int row = 0; row < ui->ncars; row++) {
            int i = ui->page == IGN_RACE_UI_TOTAL_SCORE ? ui->standings[row] : ui->order[row];
            char name[16], value[32], label[48];
            if (btz) ign_btz_name(btz, i, name);
            else snprintf(name, sizeof name, i ? "CPU %d" : "PLAYER", i);
            snprintf(label, sizeof label, "%d. %s", row + 1, name);
            if (ui->page == IGN_RACE_UI_TRACK_SCORE) snprintf(value, sizeof value, "%d", ui->points[i]);
            else if (ui->page == IGN_RACE_UI_TOTAL_SCORE)
                snprintf(value, sizeof value, "%d", ui->total[i] + (ui->committed ? 0 : ui->points[i]));
            else if (cars[i]->out && !cars[i]->wrecked) ign_hud_format_time(value, cars[i]->race_elapsed);
            else snprintf(value, sizeof value, "--:--:--");
            int y = 38 + row * step;
            const IgnFont *font = step < ui->normal.height ? &ui->small :
                                  i == 0 ? &ui->selected : &ui->normal;
            text(frame, font, label, left + 40, y, 0, lut);
            text(frame, &ui->small, value, left + 225, y + 3, 0, lut);
        }
        const char *prompt = ui->mode == 0 ? race_text_continue[lang][0] : race_text_again[lang][0];
        text(frame, &ui->small, prompt, frame->w / 2, frame->h - 14, 1, lut);
    }
}
