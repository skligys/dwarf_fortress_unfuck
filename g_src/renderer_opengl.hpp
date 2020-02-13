// STANDARD
class renderer_opengl : public renderer {
public:
  virtual bool uses_opengl() { return true; }
  
protected:
  SDL_Window *window = NULL;
  SDL_Renderer *renderer = NULL;
  SDL_GLContext gl_context = NULL;

  int dispx, dispy; // Cache of the current font size
  
  bool init_video(int w, int h) {
    {
      // SK: Debug.
      std::stringstream ss;
      ss << ">>>>> renderer_opengl::init_video(w: " << w << ", h: " << h << ")";
      gamelog_string(ss.str());
    }

    if (!window || !renderer) {
      // Get ourselves an opengl-enabled SDL window
      Uint32 flags = SDL_WINDOW_OPENGL;

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

    if (!gl_context) {
      gl_context = SDL_GL_CreateContext(window);
      if (gl_context == NULL) {
        cout << "SDL_GL_CreateContext FAILED! " << SDL_GetError() << endl;
        return false;
      }

      if (init.window.flag.has_flag(INIT_WINDOW_FLAG_VSYNC_ON)) {
        // Try addaptive vsync, fall back to regular vsync.
        int rc = SDL_GL_SetSwapInterval(-1);
        if (rc != 0) {
          rc = SDL_GL_SetSwapInterval(1);
          if (rc != 0) {
            cout << "SDL_GL_SetSwapInterval FAILED! " << SDL_GetError() << endl;
            return false;
          }
        }
      }

      // SDL 2 always double-buffers automatically.
      // Test double-buffering status
      int test;
      SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &test);
      if (test != ((init.display.flag.has_flag(INIT_DISPLAY_FLAG_SINGLE_BUFFER)) ? 0 : 1)) {
        if (enabler.is_fullscreen());
          //errorlog << "Requested single-buffering not available\n" << flush;
        else
          report_error("OpenGL", "Requested single-buffering not available");
      }

      // (Re)initialize GLEW. Technically only needs to be done once on
      // linux, but on windows forgetting will cause crashes.
      glewInit();
    }

    // Set the viewport and clear
    int gl_w, gl_h;
    SDL_GL_GetDrawableSize(window, &gl_w, &gl_h);
    glViewport(0, 0, gl_w, gl_h);
    glClear(GL_COLOR_BUFFER_BIT);

    {
      // SK: Debug.
      std::stringstream ss;
      ss << "----- renderer_opengl::init_video(), GL drawable w: " << gl_w << ", h: " << gl_h;
      gamelog_string(ss.str());
    }

    // SK: Debug.
    gamelog_string("<<<<< renderer_opengl::init_video()");

    return true;
  }

  // Vertexes, foreground color, background color, texture coordinates
  GLfloat *vertexes, *fg, *bg, *tex;

  void write_tile_vertexes(GLfloat x, GLfloat y, GLfloat *vertex) {
    vertex[0]  = x;   // Upper left
    vertex[1]  = y;
    vertex[2]  = x+1; // Upper right
    vertex[3]  = y;
    vertex[4]  = x;   // Lower left
    vertex[5]  = y+1;
    vertex[6]  = x;   // Lower left again (triangle 2)
    vertex[7]  = y+1;
    vertex[8]  = x+1; // Upper right
    vertex[9]  = y;
    vertex[10] = x+1; // Lower right
    vertex[11] = y+1;
  }

  virtual void allocate(int tiles) {
    vertexes = static_cast<GLfloat*>(realloc(vertexes, sizeof(GLfloat) * tiles * 2 * 6));
    assert(vertexes);
    fg = static_cast<GLfloat*>(realloc(fg, sizeof(GLfloat) * tiles * 4 * 6));
    assert(fg);
    bg = static_cast<GLfloat*>(realloc(bg, sizeof(GLfloat) * tiles * 4 * 6));
    assert(bg);
    tex = static_cast<GLfloat*>(realloc(tex, sizeof(GLfloat) * tiles * 2 * 6));
    assert(tex);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertexes);
  }
  
  virtual void init_opengl() {
    enabler.textures.upload_textures();
  }

  virtual void uninit_opengl() {
    enabler.textures.remove_uploaded_textures();
  }
  
  virtual void draw(int vertex_count) {
    // Render the background colors
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glColorPointer(4, GL_FLOAT, 0, bg);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count);
    // Render the foreground, colors and textures both
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_NOTEQUAL, 0);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexCoordPointer(2, GL_FLOAT, 0, tex);
    glColorPointer(4, GL_FLOAT, 0, fg);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count);
    
    printGLError();
  }

  void write_tile_arrays(int x, int y, GLfloat *fg, GLfloat *bg, GLfloat *tex) {
    Either<texture_fullid,texture_ttfid> id = screen_to_texid(x, y);
    if (id.isL) {          // An ordinary tile
      const gl_texpos *txt = enabler.textures.gl_texpos;
      // TODO: Only bother to set the one that's actually read in flat-shading mode
      // And set flat-shading mode.
      for (int i = 0; i < 6; i++) {
        *(fg++) = id.left.r;
        *(fg++) = id.left.g;
        *(fg++) = id.left.b;
        *(fg++) = 1;
        
        *(bg++) = id.left.br;
        *(bg++) = id.left.bg;
        *(bg++) = id.left.bb;
        *(bg++) = 1;
      }
      // Set texture coordinates
      *(tex++) = txt[id.left.texpos].left;   // Upper left
      *(tex++) = txt[id.left.texpos].bottom;
      *(tex++) = txt[id.left.texpos].right;  // Upper right
      *(tex++) = txt[id.left.texpos].bottom;
      *(tex++) = txt[id.left.texpos].left;   // Lower left
      *(tex++) = txt[id.left.texpos].top;
      
      *(tex++) = txt[id.left.texpos].left;   // Lower left
      *(tex++) = txt[id.left.texpos].top;
      *(tex++) = txt[id.left.texpos].right;  // Upper right
      *(tex++) = txt[id.left.texpos].bottom;
      *(tex++) = txt[id.left.texpos].right;  // Lower right
      *(tex++) = txt[id.left.texpos].top;
    } else {
      // TODO
    }
  }
  
public:
  void update_tile(int x, int y) {
    const int tile = x*gps.dimy + y;
    // Update the arrays
    GLfloat *fg  = this->fg + tile * 4 * 6;
    GLfloat *bg  = this->bg + tile * 4 * 6;
    GLfloat *tex = this->tex + tile * 2 * 6;
    write_tile_arrays(x, y, fg, bg, tex);
  }

  void update_all() {
    glClear(GL_COLOR_BUFFER_BIT);
    for (int x = 0; x < gps.dimx; x++)
      for (int y = 0; y < gps.dimy; y++)
        update_tile(x, y);
  }
  
  void render() {
    draw(gps.dimx*gps.dimy*6);
    if (init.display.flag.has_flag(INIT_DISPLAY_FLAG_ARB_SYNC) && GL_ARB_sync) {
      assert(enabler.sync == NULL);
      enabler.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    SDL_GL_SwapWindow(window);
  }

  renderer_opengl() {
    // Init member variables so realloc'll work
    screen   = NULL;
    vertexes = NULL;
    fg       = NULL;
    bg       = NULL;
    tex      = NULL;
    zoom_steps = forced_steps = 0;
    
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

    // Initialize opengl
    init_opengl();
  }

  virtual ~renderer_opengl() {
    // SK: Debug.
    gamelog_string(">>>>> renderer_opengl::~renderer_opengl()");

    free(vertexes);
    free(fg);
    free(bg);
    free(tex);
    if (gl_context) SDL_GL_DeleteContext(gl_context);
    if (renderer) {
      SDL_DestroyRenderer(renderer);
      renderer = NULL;
    }
    if (window) {
      SDL_DestroyWindow(window);
      window = NULL;
    }

    // SK: Debug.
    gamelog_string("<<<<< renderer_opengl::~renderer_opengl()");
  }

  int zoom_steps, forced_steps;
  int natural_w, natural_h; // How large our view would be if it wasn't zoomed

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

  // Parameters: grid units
  void reshape(pair<int,int> size) {
    int w = MIN(MAX(size.first, MIN_GRID_X), MAX_GRID_X);
    int h = MIN(MAX(size.second, MIN_GRID_Y), MAX_GRID_Y);
#ifdef DEBUG
    cout << "Resizing grid to " << w << "x" << h << endl;
#endif
    gps_allocate(w, h);
    reshape_gl();
  }

  int off_x, off_y, size_x, size_y;
  
  bool get_mouse_coords(int &x, int &y) {
    int mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    mouse_x -= off_x; mouse_y -= off_y;
    if (mouse_x < 0 || mouse_y < 0 ||
        mouse_x >= size_x || mouse_y >= size_y)
      return false; // Out of bounds
    x = double(mouse_x) / double(size_x) * double(gps.dimx);
    y = double(mouse_y) / double(size_y) * double(gps.dimy);
    return true;
  }

  virtual void reshape_gl() {
    // Allocate array memory
    allocate(gps.dimx * gps.dimy);
    // Initialize the vertex array
    int tile = 0;
    for (GLfloat x = 0; x < gps.dimx; x++)
      for (GLfloat y = 0; y < gps.dimy; y++, tile++)
        write_tile_vertexes(x, y, vertexes + 6*2*tile);
    // Setup invariant state
    glEnableClientState(GL_COLOR_ARRAY);
    /// Set up our coordinate system
    int gl_w, gl_h;
    SDL_GL_GetDrawableSize(window, &gl_w, &gl_h);
    if (forced_steps + zoom_steps == 0 &&
        init.display.flag.has_flag(INIT_DISPLAY_FLAG_BLACK_SPACE)) {
      size_x = gps.dimx * dispx;
      size_y = gps.dimy * dispy;
      off_x = (gl_w - size_x) / 2;
      off_y = (gl_h - size_y) / 2;
    } else {
      // If we're zooming (or just not using black space), we use the
      // entire window.
      size_x = gl_w;
      size_y = gl_h;
      off_x = off_y = 0;
    }
    glViewport(off_x, off_y, size_x, size_y);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, gps.dimx, gps.dimy, 0);
  }

  // Parameters: window size
  void resize(int w, int h) {
    // (Re)calculate grid-size
    dispx = enabler.is_fullscreen() ?
      init.font.large_font_dispx :
      init.font.small_font_dispx;
    dispy = enabler.is_fullscreen() ?
      init.font.large_font_dispy :
      init.font.small_font_dispy;
    natural_w = MAX(w / dispx,1);
    natural_h = MAX(h / dispy,1);
    // Compute forced_steps so we satisfy our grid-size limits
    compute_forced_zoom();
    // Force a full display cycle
    gps.force_full_display_count = 1;
    enabler.flag |= ENABLERFLAG_RENDER;
    // Reinitialize the video
    uninit_opengl();
    init_video(w, h);
    init_opengl();
    // Only reshape if we're free to pick grid size
    if (enabler.overridden_grid_sizes.size() == 0)
      reshape(compute_zoom());
  }

  // Parameters: grid size
  void grid_resize(int w, int h) {
    reshape(make_pair(w, h));
  }

public:
  void set_fullscreen() {
    if (enabler.is_fullscreen()) {
      int gl_w, gl_h;
      SDL_GL_GetDrawableSize(window, &gl_w, &gl_h);
      init.display.desired_windowed_width = gl_w;
      init.display.desired_windowed_height = gl_h;
      resize(init.display.desired_fullscreen_width,
             init.display.desired_fullscreen_height);
    } else {
      resize(init.display.desired_windowed_width, init.display.desired_windowed_height);
    }
  }
};

// Specialization for PARTIAL:0
class renderer_once : public renderer_opengl {
  int tile_count;
  
protected:
  void update_tile(int x, int y) {
    write_tile_vertexes(x, y, vertexes + tile_count * 6 * 2);
    write_tile_arrays(x, y,
                      fg + tile_count * 6 * 4,
                      bg + tile_count * 6 * 4,
                      tex + tile_count * 6 * 2);
    tile_count++;
  }

  void draw(int dummy) {
    renderer_opengl::draw(tile_count*6);
    tile_count = 0;
  }

public:
  renderer_once() {
    tile_count = 0;
  }
};

// PARTIAL:N
class renderer_partial : public renderer_opengl {
  int buffersz;
  list<int> erasz; // Previous eras
  int current_erasz; // And the current one
  int sum_erasz;
  int head, tail; // First unused tile, first used tile respectively
  int redraw_count; // Number of eras to max out at

  void update_tile(int x, int y) {
    write_tile_vertexes(x, y, vertexes + head * 6 * 2);
    write_tile_arrays(x, y,
                      fg + head * 6 * 4,
                      bg + head * 6 * 4,
                      tex + head * 6 * 2);
    head = (head + 1) % buffersz;
    current_erasz++; sum_erasz++;
    if (head == tail) {
      //gamelog << "Expanding partial-printing buffer" << endl;
      // Buffer is full, expand it.
      renderer_opengl::allocate(buffersz * 2);
      // Move the tail to the end of the newly allocated space
      tail += buffersz;
      memmove(vertexes + tail * 6 * 2, vertexes + head * 6 * 2, sizeof(GLfloat) * 6 * 2 * (buffersz - head));
      memmove(fg + tail * 6 * 4, fg + head * 6 * 4, sizeof(GLfloat) * 6 * 4 * (buffersz - head));
      memmove(bg + tail * 6 * 4, fg + head * 6 * 4, sizeof(GLfloat) * 6 * 4 * (buffersz - head));
      memmove(tex + tail * 6 * 2, fg + head * 6 * 2, sizeof(GLfloat) * 6 * 2 * (buffersz - head));
      // And finish.
      buffersz *= 2;
    }
  }

  void allocate(int tile_count) {
    assert(false);
  }
  
  virtual void reshape_gl() {
    // TODO: This function is duplicate code w/base class reshape_gl
    // Setup invariant state
    glEnableClientState(GL_COLOR_ARRAY);
    /// Set up our coordinate system
    int gl_w, gl_h;
    SDL_GL_GetDrawableSize(window, &gl_w, &gl_h);
    if (forced_steps + zoom_steps == 0 &&
        init.display.flag.has_flag(INIT_DISPLAY_FLAG_BLACK_SPACE)) {
      size_x = gps.dimx * dispx;
      size_y = gps.dimy * dispy;
      off_x = (gl_w - size_x) / 2;
      off_y = (gl_h - size_y) / 2;
    } else {
      // If we're zooming (or just not using black space), we use the
      // entire window.
      size_x = gl_w;
      size_y = gl_h;
      off_x = off_y = 0;
    }
    glViewport(off_x, off_y, size_x, size_y);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, gps.dimx, gps.dimy, 0);
  }

  void draw_arrays(GLfloat *vertexes, GLfloat *fg, GLfloat *bg, GLfloat *tex, int tile_count) {
    // Set vertex pointer
    glVertexPointer(2, GL_FLOAT, 0, vertexes);
    // Render the background colors
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glColorPointer(4, GL_FLOAT, 0, bg);
    glDrawArrays(GL_TRIANGLES, 0, tile_count * 6);
    // Render the foreground, colors and textures both
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_NOTEQUAL, 0);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorPointer(4, GL_FLOAT, 0, fg);
    glTexCoordPointer(2, GL_FLOAT, 0, tex);
    glDrawArrays(GL_TRIANGLES, 0, tile_count * 6);
  }

  void draw(int dummy) {
    if (tail > head) {
      // We're straddling the end of the array, so have to do this in two steps
      draw_arrays(vertexes + tail * 6 * 2,
                  fg + tail * 6 * 4,
                  bg + tail * 6 * 4,
                  tex + tail * 6 * 2,
                  buffersz - tail);
      draw_arrays(vertexes, fg, bg, tex, head-1);
    } else {
      draw_arrays(vertexes + tail * 6 * 2,
                  fg + tail * 6 * 4,
                  bg + tail * 6 * 4,
                  tex + tail * 6 * 2,
                  sum_erasz);
    }
    
    printGLError();
    erasz.push_back(current_erasz); current_erasz = 0;
    if (erasz.size() == redraw_count) {
      // Right, time to retire the oldest era.
      tail = (tail + erasz.front()) % buffersz;
      sum_erasz -= erasz.front();
      erasz.pop_front();
    }
  }
  
public:
  renderer_partial() {
    redraw_count = init.display.partial_print_count;
    buffersz = 2048;
    renderer_opengl::allocate(buffersz);
    current_erasz = head = tail = sum_erasz = 0;
  }
};

class renderer_accum_buffer : public renderer_once {
  void draw(int vertex_count) {
    // Copy the previous frame's buffer back in
    glAccum(GL_RETURN, 1);
    renderer_once::draw(vertex_count);
    // Store the screen contents back to the buffer
    glAccum(GL_LOAD, 1);
  }
};

class renderer_framebuffer : public renderer_once {
  GLuint framebuffer, fb_texture;
  
  void init_opengl() {
    glGenFramebuffersEXT(1, &framebuffer);
    // Allocate FBO texture memory
    glGenTextures(1, &fb_texture);
    glBindTexture(GL_TEXTURE_2D, fb_texture);
    int gl_w, gl_h;
    SDL_GL_GetDrawableSize(window, &gl_w, &gl_h);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 gl_w, gl_h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    
    // Bind texture to FBO
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, framebuffer);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                              GL_TEXTURE_2D, fb_texture, 0);
    renderer_once::init_opengl();
  }

  void uninit_opengl() {
    renderer_once::uninit_opengl();
    glDeleteTextures(1, &fb_texture);
    glDeleteFramebuffersEXT(1, &framebuffer);
  }

  void draw(int vertex_count) {
    // Bind the framebuffer
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, framebuffer);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, 0);
    // Draw
    renderer_once::draw(vertex_count);
    // Draw the framebuffer to screen
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, 0);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, framebuffer);
    int gl_w, gl_h;
    SDL_GL_GetDrawableSize(window, &gl_w, &gl_h);
    glBlitFramebufferEXT(0,0, gl_w, gl_h,
                         0,0, gl_w, gl_h,
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
    printGLError();
  }
};

class renderer_vbo : public renderer_opengl {
  GLuint vbo; // Vertexes only

  void init_opengl() {
    renderer_opengl::init_opengl();
    glGenBuffersARB(1, &vbo);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, gps.dimx*gps.dimy*6*2*sizeof(GLfloat), vertexes, GL_STATIC_DRAW_ARB);
    glVertexPointer(2, GL_FLOAT, 0, 0);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
  }
  
  void uninit_opengl() {
    glDeleteBuffersARB(1, &vbo);
    renderer_opengl::uninit_opengl();
  }
};
