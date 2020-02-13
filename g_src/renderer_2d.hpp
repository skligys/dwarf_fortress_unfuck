#include "enabler.h"
#include "init.h"
#include "resize++.h"
#include "ttf_manager.hpp"

#include <iostream>
using namespace std;

void report_error(const char*, const char*);

class renderer_2d_base : public renderer {
protected:
  SDL_Window *window = NULL;
  SDL_Renderer *renderer = NULL;
  map<texture_fullid, SDL_Texture*> tile_cache;
  int dispx, dispy, dimx, dimy;
  // We may shrink or enlarge dispx/dispy in response to zoom requests. dispx/y_z are the
  // size we actually display tiles at.
  int dispx_z, dispy_z;
  // Viewport origin
  int origin_x, origin_y;

  SDL_Texture *tile_cache_lookup(texture_fullid &id, bool convert=true) {
    map<texture_fullid, SDL_Texture*>::iterator it = tile_cache.find(id);
    if (it != tile_cache.end()) {
      return it->second;
    } else {
      // Create the colorized texture
      SDL_Surface *tex   = enabler.textures.get_texture_data(id.texpos);
      SDL_Surface *color;
      color = SDL_CreateRGBSurface(SDL_SWSURFACE,
                                   tex->w, tex->h,
                                   tex->format->BitsPerPixel,
                                   tex->format->Rmask,
                                   tex->format->Gmask,
                                   tex->format->Bmask,
                                   0);
      if (!color) {
        MessageBox (NULL, "Unable to create texture!", "Fatal error", MB_OK | MB_ICONEXCLAMATION);
        abort();
      }
      
      // Fill it
      Uint32 color_fgi = SDL_MapRGB(color->format, id.r*255, id.g*255, id.b*255);
      Uint8 *color_fg = (Uint8*) &color_fgi;
      Uint32 color_bgi = SDL_MapRGB(color->format, id.br*255, id.bg*255, id.bb*255);
      Uint8 *color_bg = (Uint8*) &color_bgi;
      SDL_LockSurface(tex);
      SDL_LockSurface(color);
      
      Uint8 *pixel_src, *pixel_dst;
      for (int y = 0; y < tex->h; y++) {
        pixel_src = ((Uint8*)tex->pixels) + (y * tex->pitch);
        pixel_dst = ((Uint8*)color->pixels) + (y * color->pitch);
        for (int x = 0; x < tex->w; x++, pixel_src+=4, pixel_dst+=4) {
          float alpha = pixel_src[3] / 255.0;
          for (int c = 0; c < 3; c++) {
            float fg = color_fg[c] / 255.0, bg = color_bg[c] / 255.0, tex = pixel_src[c] / 255.0;
            pixel_dst[c] = ((alpha * (tex * fg)) + ((1 - alpha) * bg)) * 255;
          }
        }
      }
      
      SDL_UnlockSurface(color);
      SDL_UnlockSurface(tex);
      
      SDL_Surface *disp = convert ?
        SDL_Resize(color, dispx_z, dispy_z) :  // Convert to display format; deletes color
        color;  // color is not deleted, but we don't want it to be.
      SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, disp);
      SDL_FreeSurface(disp);
      // Insert and return
      tile_cache[id] = texture;
      return texture;
    }
  }

  virtual bool init_video(int w, int h) {
    if (!window || !renderer) {
      // Get ourselves a 2D SDL window
      Uint32 flags = 0;
      // Set it up for windowed or fullscreen, depending.
      if (enabler.is_fullscreen()) {
        flags |= SDL_WINDOW_FULLSCREEN;
      } else {
        if (!init.display.flag.has_flag(INIT_DISPLAY_FLAG_NOT_RESIZABLE))
          flags |= SDL_WINDOW_RESIZABLE;
      }

      // (Re)create the window
      int rc = SDL_CreateWindowAndRenderer(w, h, flags, &window, &renderer);
      bool success = rc == 0 && window != NULL && renderer != NULL;
      if (!success) {
        cout << "SDL_CreateWindowAndRenderer FAILED! " << SDL_GetError() << endl;
        return false;
      }
    } else {
      int rc = SDL_SetWindowFullscreen(window, enabler.is_fullscreen() ? SDL_WINDOW_FULLSCREEN : 0);
      if (rc != 0) {
        cout << "SDL_SetWindowFullscreen FAILED! " << SDL_GetError() << endl;
        return false;
      }
    }

    return true;
  }

public:
  list<pair<SDL_Texture*,SDL_Rect> > ttfs_to_render;

  void update_tile(int x, int y) {
    // Figure out where to blit
    SDL_Rect dst;
    dst.x = dispx_z * x + origin_x;
    dst.y = dispy_z * y + origin_y;
    // Read tiles from gps, create cached texture
    Either<texture_fullid,texture_ttfid> id = screen_to_texid(x, y);
    SDL_Texture *tex;
    if (id.isL) {      // Ordinary tile, cached here
      tex = tile_cache_lookup(id.left);
      // And blit.
      SDL_RenderCopy(renderer, tex, NULL, &dst);
    } else {  // TTF, cached in ttf_manager so no point in also caching here
      tex = ttf_manager.get_texture(id.right);
      // Blit later
      ttfs_to_render.push_back(make_pair(tex, dst));
    }
  }

  void update_all() {
    SDL_RenderClear(renderer);
    for (int x = 0; x < gps.dimx; x++)
      for (int y = 0; y < gps.dimy; y++)
        update_tile(x, y);
  }

  virtual void render() {
    // Render the TTFs, which we left for last
    for (auto it = ttfs_to_render.begin(); it != ttfs_to_render.end(); ++it) {
      SDL_RenderCopy(renderer, it->first, NULL, &it->second);
    }
    ttfs_to_render.clear();
    // And present changes.
    SDL_RenderPresent(renderer);
  }

  virtual ~renderer_2d_base() {
	for (auto it = tile_cache.cbegin(); it != tile_cache.cend(); ++it)
		SDL_DestroyTexture(it->second);
	for (auto it = ttfs_to_render.cbegin(); it != ttfs_to_render.cend(); ++it)
		SDL_DestroyTexture(it->first);
    if (renderer) {
      SDL_DestroyRenderer(renderer);
      renderer = NULL;
    }
    if (window) {
      SDL_DestroyWindow(window);
      window = NULL;
    }
}

  void grid_resize(int w, int h) {
    dimx = w; dimy = h;
    // Only reallocate the grid if it actually changes
    if (init.display.grid_x != dimx || init.display.grid_y != dimy)
      gps_allocate(dimx, dimy);
    // But always force a full display cycle
    gps.force_full_display_count = 1;
    enabler.flag |= ENABLERFLAG_RENDER;    
  }

  renderer_2d_base() {
    zoom_steps = forced_steps = 0;
  }
  
  int zoom_steps, forced_steps;
  int natural_w, natural_h;

  void compute_forced_zoom() {
    forced_steps = 0;
    pair<int,int> zoomed = compute_zoom();
    while (zoomed.first < MIN_GRID_X || zoomed.second < MIN_GRID_Y) {
      forced_steps++;
      zoomed = compute_zoom();
    }
    while (zoomed.first > MAX_GRID_X || zoomed.second > MAX_GRID_Y) {
      forced_steps--;
      zoomed = compute_zoom();
    }
  }

  pair<int,int> compute_zoom(bool clamp = false) {
    const int dispx = enabler.is_fullscreen() ?
      init.font.large_font_dispx :
      init.font.small_font_dispx;
    const int dispy = enabler.is_fullscreen() ?
      init.font.large_font_dispy :
      init.font.small_font_dispy;
    int w, h;
    if (dispx < dispy) {
      w = natural_w + zoom_steps + forced_steps;
      h = double(natural_h) * (double(w) / double(natural_w));
    } else {
      h = natural_h + zoom_steps + forced_steps;
      w = double(natural_w) * (double(h) / double(natural_h));
    }
    if (clamp) {
      w = MIN(MAX(w, MIN_GRID_X), MAX_GRID_X);
      h = MIN(MAX(h, MIN_GRID_Y), MAX_GRID_Y);
    }
    return make_pair(w,h);
  }

  void resize(int w, int h) {
    if (w < 0 || h < 0) {
      SDL_GetWindowSize(window, &w, &h);
    }
    if (w == window_w && h == window_h) return;

    // We've gotten resized.. first step is to reinitialize video
    cout << "New window size: " << w << "x" << h << endl;
    init_video(w, h);
    dispx = enabler.is_fullscreen() ?
      init.font.large_font_dispx :
      init.font.small_font_dispx;
    dispy = enabler.is_fullscreen() ?
      init.font.large_font_dispy :
      init.font.small_font_dispy;
    cout << "Font size: " << dispx << "x" << dispy << endl;
    // If grid size is currently overridden, we don't change it
    if (enabler.overridden_grid_sizes.size() == 0) {
      // (Re)calculate grid-size
      dimx = MIN(MAX(w / dispx, MIN_GRID_X), MAX_GRID_X);
      dimy = MIN(MAX(h / dispy, MIN_GRID_Y), MAX_GRID_Y);
      cout << "Resizing grid to " << dimx << "x" << dimy << endl;
      grid_resize(dimx, dimy);
    }
    // Calculate zoomed tile size
    natural_w = MAX(w / dispx,1);
    natural_h = MAX(h / dispy,1);
    compute_forced_zoom();
    reshape(compute_zoom(true));
    cout << endl;
  }

  void reshape(pair<int,int> max_grid) {
    int w = max_grid.first,
      h = max_grid.second;
    int window_w = 0, window_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    // Compute the largest tile size that will fit this grid into the window, roughly maintaining aspect ratio
    double try_x = dispx, try_y = dispy;
    try_x = window_w / w;
    try_y = MIN(try_x / dispx * dispy, window_h / h);
    try_x = MIN(try_x, try_y / dispy * dispx);
    dispx_z = MAX(1,try_x); dispy_z = MAX(try_y,1);
    cout << "Resizing font to " << dispx_z << "x" << dispy_z << endl;
    // Remove now-obsolete tile catalog
    for (map<texture_fullid, SDL_Texture*>::iterator it = tile_cache.begin();
         it != tile_cache.end();
         ++it)
      SDL_DestroyTexture(it->second);
    tile_cache.clear();
    // Recompute grid based on the new tile size
    w = CLAMP(window_w / dispx_z, MIN_GRID_X, MAX_GRID_X);
    h = CLAMP(window_h / dispy_z, MIN_GRID_Y, MAX_GRID_Y);
    // Reset grid size
#ifdef DEBUG
    cout << "Resizing grid to " << w << "x" << h << endl;
#endif
    gps_allocate(w,h);
    // Force redisplay
    gps.force_full_display_count = 1;
    // Calculate viewport origin, for centering
    origin_x = (window_w - dispx_z * w) / 2;
    origin_y = (window_h - dispy_z * h) / 2;
    // Reset TTF rendering
    ttf_manager.init(dispy_z, dispx_z);
  }

private:
  
  void set_fullscreen() {
    if (enabler.is_fullscreen()) {
      int window_w = 0, window_h = 0;
      SDL_GetWindowSize(window, &window_w, &window_h);
      init.display.desired_windowed_width = window_w;
      init.display.desired_windowed_height = window_h;
      resize(init.display.desired_fullscreen_width,
             init.display.desired_fullscreen_height);
    } else {
      resize(init.display.desired_windowed_width, init.display.desired_windowed_height);
    }
  }

  bool get_mouse_coords(int &x, int &y) {
    bool has_mouse_focus = (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
    if (!has_mouse_focus) return false;

    int mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    mouse_x -= origin_x; mouse_y -= origin_y;
    if (mouse_x < 0 || mouse_x >= dispx_z*dimx ||
        mouse_y < 0 || mouse_y >= dispy_z*dimy)
      return false;
    x = mouse_x / dispx_z;
    y = mouse_y / dispy_z;
    return true;
  }

  void zoom(zoom_commands cmd) {
    pair<int,int> before = compute_zoom(true);
    int before_steps = zoom_steps;
    switch (cmd) {
    case zoom_in:    zoom_steps -= init.input.zoom_speed; break;
    case zoom_out:   zoom_steps += init.input.zoom_speed; break;
    case zoom_reset:
      zoom_steps = 0;
    case zoom_resetgrid:
      compute_forced_zoom();
      break;
    }
    pair<int,int> after = compute_zoom(true);
    if (after == before && (cmd == zoom_in || cmd == zoom_out))
      zoom_steps = before_steps;
    else
      reshape(after);
  }
  
};

class renderer_2d : public renderer_2d_base {
public:
  renderer_2d() {
    // Set window title/icon.
    SDL_SetWindowTitle(window, GAME_TITLE_STRING);
    SDL_Surface *icon = IMG_Load("data/art/icon.png");
    if (icon != NULL) {
      SDL_SetWindowIcon(window, icon);
      // The icon's surface doesn't get used past this point.
      SDL_FreeSurface(icon);
    }
    
    // Find the current desktop resolution if fullscreen resolution is auto
    if (init.display.desired_fullscreen_width  == 0 ||
        init.display.desired_fullscreen_height == 0) {
      SDL_DisplayMode mode;
      SDL_GetDisplayMode(0, 0, &mode);
      init.display.desired_fullscreen_width = mode.w;
      init.display.desired_fullscreen_height = mode.h;
    }

    // Initialize our window
    bool worked = init_video(enabler.is_fullscreen() ?
                             init.display.desired_fullscreen_width :
                             init.display.desired_windowed_width,
                             enabler.is_fullscreen() ?
                             init.display.desired_fullscreen_height :
                             init.display.desired_windowed_height);

    // Fallback to windowed mode if fullscreen fails
    if (!worked && enabler.is_fullscreen()) {
      enabler.fullscreen = false;
      report_error("SDL initialization failure, trying windowed mode", SDL_GetError());
      worked = init_video(init.display.desired_windowed_width,
                          init.display.desired_windowed_height);
    }
    // Quit if windowed fails
    if (!worked) {
      report_error("SDL initialization failure", SDL_GetError());
      exit(EXIT_FAILURE);
    }
  }
};

class renderer_offscreen : public renderer_2d_base {
  SDL_Surface *screen = NULL;
  virtual bool init_video(int, int);
public:
  virtual ~renderer_offscreen();
  renderer_offscreen(int, int);
  void update_all(int, int);
  void save_to_file(const string &file);
};
