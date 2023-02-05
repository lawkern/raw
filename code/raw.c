/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#define RESOLUTION_BASE_WIDTH  320
#define RESOLUTION_BASE_HEIGHT 240

#define function static
#define global static

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

#define PLATFORM_LOG(name) name(char *format, ...)

typedef uint32_t u32;
typedef  int32_t s32;

struct render_bitmap
{
   u32 width;
   u32 height;

   u32 *memory;
};

struct user_input
{
   s32 mouse_x;
   s32 mouse_y;

   bool control_scroll;
   float scroll_delta;

   bool mouse_left;
   bool mouse_middle;
   bool mouse_right;

   bool function_keys[13];
};

function void
update(struct render_bitmap *bitmap, struct user_input *input, float frame_seconds_elapsed)
{
   u32 width = bitmap->width;
   u32 height = bitmap->height;

   for(u32 y = 0; y < height; ++y)
   {
      for(u32 x = 0; x < width; ++x)
      {
         bitmap->memory[(y * width) + x] = 0xFF0000FF;
      }
   }
}
