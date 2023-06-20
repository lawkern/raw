/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#define LINUX_LOG_MAX_LENGTH 1024

typedef sem_t platform_semaphore;

#include "raw.c"
#include "renderer_opengl.c"

#define LINUX_SECONDS_ELAPSED(start, end) ((float)((end).tv_sec - (start).tv_sec) \
        + (1e-9f * (float)((end).tv_nsec - (start).tv_nsec)))

struct linux_window_dimensions
{
   s32 width;
   s32 height;
};

global bool linux_global_is_running;
global bool linux_global_is_paused;
global Display *linux_global_display;

function
PLATFORM_LOG(platform_log)
{
   char message[LINUX_LOG_MAX_LENGTH];

   va_list arguments;
   va_start(arguments, format);
   {
      vsnprintf(message, sizeof(message), format, arguments);
   }
   va_end(arguments);

   printf("%s", message);
}

function
PLATFORM_ENQUEUE_WORK(platform_enqueue_work)
{
   u32 new_write_index = (queue->write_index + 1) % ARRAY_LENGTH(queue->entries);
   assert(new_write_index != queue->read_index);

   struct queue_entry *entry = queue->entries + queue->write_index;
   entry->data = data;
   entry->callback = callback;

   queue->completion_target++;

   asm volatile("" ::: "memory");

   queue->write_index = new_write_index;
   sem_post(&queue->semaphore);
}

function bool
linux_dequeue_work(struct platform_work_queue *queue)
{
   // NOTE(law): Return whether this thread should be made to wait until more
   // work becomes available.

   u32 read_index = queue->read_index;
   u32 new_read_index = (read_index + 1) % ARRAY_LENGTH(queue->entries);
   if(read_index == queue->write_index)
   {
      return(true);
   }

   u32 index = __sync_val_compare_and_swap(&queue->read_index, read_index, new_read_index);
   if(index == read_index)
   {
      struct queue_entry entry = queue->entries[index];
      entry.callback(queue, entry.data);

      __sync_add_and_fetch(&queue->completion_count, 1);
   }

   return(false);
}

function
PLATFORM_COMPLETE_QUEUE(platform_complete_queue)
{
   while(queue->completion_target > queue->completion_count)
   {
      linux_dequeue_work(queue);
   }

   queue->completion_target = 0;
   queue->completion_count = 0;
}

function void *
linux_thread_procedure(void *data)
{
   struct platform_work_queue *queue = (struct platform_work_queue *)data;
   platform_log("Worker thread launched.\n");

   while(1)
   {
      if(linux_dequeue_work(queue))
      {
         sem_wait(&queue->semaphore);
      }
   }

   platform_log("Worker thread terminated.\n");

   return(0);
}

function void *
linux_allocate(size_t size)
{
   // NOTE(law): munmap() requires the size of the allocation in order to free
   // the virtual memory. This function smuggles the allocation size just before
   // the address that it actually returns.

   size_t allocation_size = size + sizeof(size_t);
   void *allocation = mmap(0, allocation_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);

   if(allocation == MAP_FAILED)
   {
      platform_log("ERROR: Linux failed to allocate virtual memory.");
      return(0);
   }

   *(size_t *)allocation = allocation_size;

   void *result = (void *)((u8 *)allocation + sizeof(size_t));
   return(result);
}

function void
linux_deallocate(void *memory)
{
   // NOTE(law): munmap() requires the size of the allocation in order to free
   // the virtual memory. We always just want to dump the entire thing, so
   // allocate() hides the allocation size just before the address it returns.

   void *allocation = (void *)((u8 *)memory - sizeof(size_t));
   size_t allocation_size = *(size_t *)allocation;

   if(munmap(allocation, allocation_size) != 0)
   {
      platform_log("ERROR: Linux failed to deallocate virtual memory.");
   }
}

function void
linux_get_window_dimensions(Window window, struct linux_window_dimensions *dimensions)
{
   Display *display = linux_global_display;

   XWindowAttributes window_attributes = {0};
   XGetWindowAttributes(display, window, &window_attributes);

   dimensions->width  = (s32)window_attributes.width;
   dimensions->height = (s32)window_attributes.height;
}

function void
linux_set_window_size(Window window, s32 client_width, s32 client_height)
{
   XResizeWindow(linux_global_display, window, client_width, client_height);
}

function Window
linux_create_window(struct render_bitmap bitmap, XVisualInfo *visual_info)
{
   Display *display = linux_global_display;
   assert(display);

   Window root = DefaultRootWindow(display);

   s32 screen_number = DefaultScreen(display);
   s32 screen_bit_depth = 24;

   XSetWindowAttributes window_attributes = {0};
   u64 attribute_mask = 0;

   window_attributes.background_pixel = 0;
   attribute_mask |= CWBackPixel;

   window_attributes.border_pixel = 0;
   attribute_mask |= CWBorderPixel;

   // NOTE(law): Seeting the bit gravity to StaticGravity prevents flickering
   // during window resize.
   window_attributes.bit_gravity = StaticGravity;
   attribute_mask |= CWBitGravity;

   window_attributes.colormap = XCreateColormap(display, root, visual_info->visual, AllocNone);
   attribute_mask |= CWColormap;

   window_attributes.event_mask = (ExposureMask |
                                   KeyPressMask |
                                   KeyReleaseMask |
                                   ButtonPressMask |
                                   ButtonReleaseMask |
                                   StructureNotifyMask |
                                   PropertyChangeMask);
   attribute_mask |= CWEventMask;

   Window window = XCreateWindow(display,
                                 root,
                                 0,
                                 0,
                                 bitmap.width,
                                 bitmap.height,
                                 0,
                                 visual_info->depth,
                                 InputOutput,
                                 visual_info->visual,
                                 attribute_mask,
                                 &window_attributes);

   assert(window);

   XStoreName(display, window, "RAW Software Renderer");

   XSizeHints size_hints = {0};
   size_hints.flags = PMinSize|PMaxSize;
   size_hints.min_width = bitmap.width / 2;
   size_hints.min_height = bitmap.height / 2;
   size_hints.max_width = bitmap.width;
   size_hints.max_height = bitmap.height;
   XSetWMNormalHints(display, window, &size_hints);

   XMapWindow(display, window);
   XFlush(display);

   return(window);
}

function Window
linux_initialize_opengl(struct render_bitmap bitmap)
{
   // TODO(law): Better checking for available GL extensions.

   Display *display = linux_global_display;
   s32 screen_number = DefaultScreen(display);

   int error_base;
   int event_base;
   Bool glx_is_supported = glXQueryExtension(display, &error_base, &event_base);
   assert(glx_is_supported);

   // NOTE(law): Get glX frame buffer configuration.
   int configuration_attributes[] =
   {
      GLX_X_RENDERABLE, True,
      GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
      GLX_RENDER_TYPE, GLX_RGBA_BIT,
      GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
      GLX_RED_SIZE, 8,
      GLX_GREEN_SIZE, 8,
      GLX_BLUE_SIZE, 8,
      GLX_ALPHA_SIZE, 8,
      GLX_DEPTH_SIZE, 24,
      GLX_STENCIL_SIZE, 8,
      GLX_DOUBLEBUFFER, True,
      // GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB, True,
      GLX_SAMPLE_BUFFERS, 1,
      GLX_SAMPLES, 4,
      None
   };

   s32 configuration_count = 0;
   GLXFBConfig *configurations = glXChooseFBConfig(display, screen_number, configuration_attributes, &configuration_count);

   GLXFBConfig configuration;
   bool found_valid_configuration = false;
   for(u32 configuration_index = 0; configuration_index < configuration_count; ++configuration_index)
   {
      configuration = configurations[configuration_index];

      XVisualInfo *visual_info = glXGetVisualFromFBConfig(display, configuration);
      if(visual_info)
      {
         s32 visual_id = visual_info->visualid;
         XFree(visual_info);

         if(visual_id)
         {
            found_valid_configuration = true;
            break;
         }
      }
   }
   assert(found_valid_configuration);
   XFree(configurations);

   XVisualInfo *visual_info = glXGetVisualFromFBConfig(display, configuration);
   Window window = linux_create_window(bitmap, visual_info);

   // NOTE(law): Load any Linux-specific OpenGL functions we need.
   typedef GLXContext opengl_function_glXCreateContextAttribsARB(Display *, GLXFBConfig, GLXContext, Bool, const int *);
   typedef       void opengl_function_glXSwapIntervalEXT(Display *, GLXDrawable, int);

   DECLARE_OPENGL_FUNCTION(glXCreateContextAttribsARB);
   DECLARE_OPENGL_FUNCTION(glXSwapIntervalEXT);

   LINUX_LOAD_OPENGL_FUNCTION(glXCreateContextAttribsARB);
   LINUX_LOAD_OPENGL_FUNCTION(glXSwapIntervalEXT);

   assert(glXCreateContextAttribsARB);

   s32 context_attributes[] =
   {
      GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
      GLX_CONTEXT_MINOR_VERSION_ARB, 3,
#if DEVELOPMENT_BUILD
      GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_DEBUG_BIT_ARB,
#endif
      GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
      None
   };

   GLXContext gl_context = glXCreateContextAttribsARB(display, configuration, 0, True, context_attributes);
   assert(gl_context);

   Bool context_attached = glXMakeCurrent(display, window, gl_context);
   assert(context_attached);

   // NOTE(law): If we have access to vsync through glX, try to turn it on.
   if(glXSwapIntervalEXT)
   {
      // TODO(law): Make it possible to toggle vsync.
      glXSwapIntervalEXT(display, window, 1);
   }

   int glx_major_version;
   int glx_minor_version;
   glXQueryVersion(display, &glx_major_version, &glx_minor_version);

   platform_log("=====\n");
   platform_log("Version (glX): %d.%d\n", glx_major_version, glx_minor_version);
   platform_log("=====\n");

   // NOTE(law): Load any OpenGL function pointers that we don't expect to have
   // by default before initializing the platform-independent OpenGL code.
#define X(name) LINUX_LOAD_OPENGL_FUNCTION(name);
   OPENGL_FUNCTION_POINTERS;
#undef X

   // NOTE(law): Initialize the platform-independent side of OpenGL.
   opengl_initialize();

   return(window);
}

function void
linux_display_bitmap(Window window, struct render_bitmap bitmap)
{
   struct linux_window_dimensions dimensions;
   linux_get_window_dimensions(window, &dimensions);

   opengl_display_bitmap(&bitmap, dimensions.width, dimensions.height);

   glXSwapBuffers(linux_global_display, window);
}

function void
linux_process_input(Window window, XEvent event, struct user_input *input)
{
   // Keyboard handling:
   if(event.type == KeyPress || event.type == KeyRelease)
   {
      XKeyEvent key_event = event.xkey;
      bool alt_key_pressed = (key_event.state | XK_Meta_L | XK_Meta_R);

      char buffer[256];
      KeySym keysym;
      XLookupString(&key_event, buffer, ARRAY_LENGTH(buffer), &keysym, 0);

      if(event.type == KeyPress)
      {
         if(keysym == XK_Escape || (alt_key_pressed && keysym == XK_F4))
         {
            linux_global_is_running = false;
         }
         else if(keysym == XK_1)
         {
            linux_set_window_size(window, RESOLUTION_BASE_WIDTH, RESOLUTION_BASE_HEIGHT);
         }
         else if(keysym == XK_2)
         {
            linux_set_window_size(window, 2*RESOLUTION_BASE_WIDTH, 2*RESOLUTION_BASE_HEIGHT);
         }
         else if(keysym == XK_F1)
         {
            input->function_keys[1] = true;
         }
         else if(keysym == XK_F2)
         {
            input->function_keys[2] = true;
         }
         else if(keysym == XK_F3)
         {
            input->function_keys[3] = true;
         }
         else if(keysym == XK_F4)
         {
            input->function_keys[4] = true;
         }
         else if(keysym == XK_F5)
         {
            input->function_keys[5] = true;
         }
         else if(keysym == XK_F6)
         {
            input->function_keys[6] = true;
         }
         else if(keysym == XK_F7)
         {
            input->function_keys[7] = true;
         }
         else if(keysym == XK_F8)
         {
            input->function_keys[8] = true;
         }
         else if(keysym == XK_F9)
         {
            input->function_keys[9] = true;
         }
         else if(keysym == XK_F10)
         {
            input->function_keys[10] = true;
         }
         else if(keysym == XK_F11)
         {
            input->function_keys[11] = true;
         }
         else if(keysym == XK_F12)
         {
            input->function_keys[12] = true;
         }
      }

      if(keysym == XK_Up)
      {
         input->up = (event.type != KeyRelease);
      }
      else if(keysym == XK_Down)
      {
         input->down = (event.type != KeyRelease);
      }
      else if(keysym == XK_Left)
      {
         input->left = (event.type != KeyRelease);
      }
      else if(keysym == XK_Right)
      {
         input->right = (event.type != KeyRelease);
      }
      else if(keysym == XK_w)
      {
         input->move_up = (event.type != KeyRelease);
      }
      else if(keysym == XK_a)
      {
         input->move_left = (event.type != KeyRelease);
      }
      else if(keysym == XK_s)
      {
         input->move_down = (event.type != KeyRelease);
      }
      else if(keysym == XK_d)
      {
         input->move_right = (event.type != KeyRelease);
      }
   }

   // Mouse handling:
   if(event.type == ButtonPress || event.type == ButtonRelease)
   {
      int button = event.xbutton.button;
      if(button == Button1) // Left click
      {
         input->mouse_left = (event.type != ButtonRelease);
      }
      else if(button == Button2) // Middle click
      {
         input->mouse_middle = (event.type != ButtonRelease);
      }
      else if(button == Button3)
      {
         input->mouse_right = (event.type != ButtonRelease);
      }
      else if(button == Button4) // Scroll up
      {
         input->control_scroll = event.xbutton.state & ControlMask;
         input->scroll_delta = 1.0f;
      }
      else if(button == Button5) // Scroll down
      {
         input->control_scroll = event.xbutton.state & ControlMask;
         input->scroll_delta = -1.0f;
      }
   }
}

function void
linux_process_events(Window window, struct user_input *input)
{
   Display *display = linux_global_display;

   while(linux_global_is_running && XPending(display))
   {
      XEvent event;
      XNextEvent(display, &event);

      // NOTE(law): Prevent key repeating.
      if(event.type == KeyRelease && XEventsQueued(display, QueuedAfterReading))
      {
         XEvent next_event;
         XPeekEvent(display, &next_event);
         if(next_event.type == KeyPress &&
             next_event.xkey.time == event.xkey.time &&
             next_event.xkey.keycode == event.xkey.keycode)
         {
            XNextEvent(display, &event);
            continue;
         }
      }

      switch (event.type)
      {
         case DestroyNotify:
         {
            XDestroyWindowEvent destroy_notify_event = event.xdestroywindow;
            if(destroy_notify_event.window == window)
            {
               linux_global_is_running = false;
            }
         } break;

         case Expose:
         {
            XExposeEvent expose_event = event.xexpose;
            if(expose_event.count != 0)
            {
               continue;
            }
         } break;

         case ConfigureNotify:
         {
            s32 window_width  = event.xconfigure.width;
            s32 window_height = event.xconfigure.height;

            // TODO(law): Handle resizing the window.
         } break;

         case KeyPress:
         case KeyRelease:
         case ButtonPress:
         case ButtonRelease:
         {
            linux_process_input(window, event, input);
         } break;

         default:
         {
            // platform_log("Unhandled X11 event.\n");
         } break;
      }
   }
}

function u32
linux_get_processor_count()
{
   u32 result = sysconf(_SC_NPROCESSORS_ONLN);
   return(result);
}

int
main(int argument_count, char **arguments)
{
   struct platform_work_queue queue = {0};
   sem_init(&queue.semaphore, 0, 0);

   u32 processor_count = linux_get_processor_count();
   platform_log("%u processors currently online.\n", processor_count);

   for(long index = 1; index < processor_count; ++index)
   {
      pthread_t id;
      pthread_create(&id, 0, linux_thread_procedure, &queue);
      pthread_detach(id);
   }

   // NOTE(law) Set up the rendering bitmap.
   struct render_bitmap bitmap = {RESOLUTION_BASE_WIDTH, RESOLUTION_BASE_HEIGHT};

   size_t bytes_per_pixel = sizeof(u32);
   size_t bitmap_size = bitmap.width * bitmap.height * bytes_per_pixel;
   bitmap.memory = linux_allocate(bitmap_size);
   if(!bitmap.memory)
   {
      return(1);
   }

   // NOTE(law): Initialize the global display here.
   linux_global_display = XOpenDisplay(0);
   Window window = linux_initialize_opengl(bitmap);

   struct user_input input = {0};

   float target_seconds_per_frame = 1.0f / 60.0f;
   float frame_seconds_elapsed = 0;

   struct timespec frame_start_count;
   clock_gettime(CLOCK_MONOTONIC, &frame_start_count);

   linux_global_is_running = true;
   while(linux_global_is_running)
   {
      linux_process_events(window, &input);

      update(&bitmap, &input, &queue, frame_seconds_elapsed);

      // NOTE(law): Blit bitmap to screen.
      linux_display_bitmap(window, bitmap);

      // NOTE(law): Calculate elapsed frame time.
      struct timespec frame_end_count;
      clock_gettime(CLOCK_MONOTONIC, &frame_end_count);
      frame_seconds_elapsed = LINUX_SECONDS_ELAPSED(frame_start_count, frame_end_count);

      u32 sleep_us = 0;
      float sleep_fraction = 0.9f;
      if(frame_seconds_elapsed < target_seconds_per_frame)
      {
         sleep_us = (u32)((target_seconds_per_frame - frame_seconds_elapsed) * 1000.0f * 1000.0f * sleep_fraction);
         if(sleep_us > 0)
         {
            usleep(sleep_us);
         }
      }

      while(frame_seconds_elapsed < target_seconds_per_frame)
      {
         clock_gettime(CLOCK_MONOTONIC, &frame_end_count);
         frame_seconds_elapsed = LINUX_SECONDS_ELAPSED(frame_start_count, frame_end_count);
      }
      frame_start_count = frame_end_count;

      static u32 frame_count = 0;
      if((frame_count++ % 30) == 0)
      {
         platform_log("Frame time: %0.03fms, ", frame_seconds_elapsed * 1000.0f);
         platform_log("Sleep: %uus\n", sleep_us);
      }
   }

   XCloseDisplay(linux_global_display);

   return(0);
}
