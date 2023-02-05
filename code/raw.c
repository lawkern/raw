/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <float.h>
#include <math.h>
#include <stdint.h>

#define RESOLUTION_BASE_WIDTH  320
#define RESOLUTION_BASE_HEIGHT 240

#define function static
#define global static

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

#define PLATFORM_LOG(name) name(char *format, ...)

typedef uint8_t  u8;
typedef uint32_t u32;
typedef  int32_t s32;

struct render_bitmap
{
   u32 width;
   u32 height;

   u32 *memory;
};

typedef union
{
   struct {float x, y, z;};
   struct {float r, g, b;};
} v3;

function v3 vec3(float x, float y, float z)
{
   v3 result;

   result.x = x;
   result.y = y;
   result.z = z;

   return(result);
}

function float square(float value)
{
   float result = value * value;
   return(result);
}

function float square_root(float value)
{
   float result = (float)sqrt(value);
   return(result);
}

function float absolute_value(float value)
{
   float result = (float)fabs(value);
   return(result);
}

function v3 add3(v3 a, v3 b)
{
   a.x += b.x;
   a.y += b.y;
   a.z += b.z;

   return(a);
}

function v3 sub3(v3 a, v3 b)
{
   a.x -= b.x;
   a.y -= b.y;
   a.z -= b.z;

   return(a);
}

function v3 mul3(v3 vector, float value)
{
   vector.x *= value;
   vector.y *= value;
   vector.z *= value;

   return(vector);
}

function float dot3(v3 a, v3 b)
{
   float result = (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
   return(result);
}

function v3 cross3(v3 a, v3 b)
{
   v3 result;

   result.x = (a.y * b.z) - (a.z * b.y);
   result.y = (a.z * b.x) - (a.x * b.z);
   result.z = (a.x * b.y) - (a.y * b.x);

   return(result);
}

function float length3(v3 vector)
{
   float length_squared = dot3(vector, vector);

   float result = square_root(length_squared);
   return(result);
}

function v3 normalize3(v3 vector)
{
   float inverse_length = 1.0f / length3(vector);

   v3 result = mul3(vector, inverse_length);
   return(result);
}

function v3 noz3(v3 vector)
{
   v3 result = {0, 0, 0};

   float epsilon_squared = square(0.0001f);
   float length_squared = dot3(vector, vector);

   if(length_squared > epsilon_squared)
   {
      result = mul3(vector, 1.0f / square_root(length_squared));
   }

   return(result);
}

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

struct plane
{
   float distance;
   v3 normal;
   v3 color;
};

global struct
{
   bool is_initialized;

   u32 plane_count;
   struct plane planes[32];
} scene;

function void
update(struct render_bitmap *bitmap, struct user_input *input, float frame_seconds_elapsed)
{
   if(!scene.is_initialized)
   {
      scene.is_initialized = true;

      struct plane *p;

      p = scene.planes + scene.plane_count++;
      p->distance = 0;
      p->normal = vec3(0, 0, 1);
      p->color = vec3(0, 1, 0);

      p = scene.planes + scene.plane_count++;
      p->distance = 0;
      p->normal = vec3(0.1f, 0.1f, 1);
      p->color = vec3(1, 0, 0);

      p = scene.planes + scene.plane_count++;
      p->distance = 0;
      p->normal = vec3(-0.1f, 0.2f, 1);
      p->color = vec3(1, 1, 1);
   }

   u32 bitmap_width = bitmap->width;
   u32 bitmap_height = bitmap->height;

   v3 origin = {0, 0, 0};
   v3 camera_position = {0, 15.0f, 1.5f};

   // Both camera-space and world-space are represented using right-hand
   // coordinate systems. The camera is set to always point at the world-space
   // origin {0, 0, 0}. The camera's y-axis points down relative to its
   // image. It's z-axis points into the scene.

   v3 camera_z = noz3(sub3(origin, camera_position));
   v3 camera_x = noz3(cross3(vec3(0, 0, -1), camera_z));
   v3 camera_y = noz3(cross3(camera_z, camera_x));

   float aspect_ratio = (float)bitmap_width / (float)bitmap_height;

   float film_width = 1.0f;
   float film_height = 1.0f / aspect_ratio;

   float focal_length = 1.0f;
   v3 film_center = add3(camera_position, mul3(camera_z, focal_length));

   for(u32 y = 0; y < bitmap_height; ++y)
   {
      float film_v = -1.0f + (2.0f * ((float)y / (float)bitmap_height));

      for(u32 x = 0; x < bitmap_width; ++x)
      {
         float film_u = -1.0f + (2.0f * ((float)x / (float)bitmap_width));

         v3 film_position = film_center;
         film_position = add3(film_position, mul3(camera_x, film_u * 0.5f * film_width));
         film_position = add3(film_position, mul3(camera_y, film_v * 0.5f * film_height));

         v3 ray_direction = noz3(sub3(film_position, camera_position));
         v3 ray_color = {0, 1, 1};

         float minimum_t = FLT_MAX;
         for(u32 plane_index = 0; plane_index < scene.plane_count; ++plane_index)
         {
            struct plane *p = scene.planes + plane_index;

            float denominator = dot3(p->normal, ray_direction);
            if(absolute_value(denominator) > 0.0001f)
            {
               float t = (-p->distance - dot3(p->normal, camera_position)) / denominator;
               if(t > 0 && t < minimum_t)
               {
                  minimum_t = t;
                  ray_color = p->color;
               }
            }
         }

         u8 r = (u8)(ray_color.r * 255.0f);
         u8 g = (u8)(ray_color.g * 255.0f);
         u8 b = (u8)(ray_color.b * 255.0f);
         u8 a = 255;

         bitmap->memory[(y * bitmap_width) + x] = (r << 16) | (g <<  8) | (b <<  0) | (a << 24);
      }
   }
}
