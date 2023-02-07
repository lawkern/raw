/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define RESOLUTION_BASE_WIDTH  320
#define RESOLUTION_BASE_HEIGHT 240

#define function static
#define global static

#define TAU32 6.28318530717959f

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#define LERP(a, t, b) (((1 - (t)) * (a)) + ((t) * (b)))

#define PLATFORM_LOG(name) void name(char *format, ...)
function PLATFORM_LOG(platform_log);

typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef  int32_t s32;
typedef  int64_t s64;

function float sine(float turns)
{
   float result = sinf(turns * TAU32);
   return(result);
}

function float cosine(float turns)
{
   float result = cosf(turns * TAU32);
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

typedef union
{
   struct {float x, y, z;};
   struct {float r, g, b;};
} v3;

typedef union
{
   struct {float x, y, z, w;};
   struct {float r, g, b, a;};
} v4;

typedef union
{
   float matrix[4][4];
} matrix4;

function v3 vec3(float x, float y, float z)
{
   v3 result;

   result.x = x;
   result.y = y;
   result.z = z;

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

function v3 lerp3(v3 a, float t, v3 b)
{
   a.x = LERP(a.x, t, b.x);
   a.y = LERP(a.y, t, b.y);
   a.z = LERP(a.z, t, b.z);

   return(a);
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

function matrix4 rotation_x_matrix4(float turns)
{
   float s = sine(turns);
   float c = cosine(turns);

   matrix4 result =
   {
      1,  0,  0,  0,
      0,  c,  s,  0,
      0, -s,  c,  0,
      0,  0,  0,  1,
   };

   return(result);
}

function matrix4 rotation_y_matrix4(float turns)
{
   float s = sine(turns);
   float c = cosine(turns);

   matrix4 result =
   {
      c,  0, -s,  0,
      0,  1,  0,  0,
      s,  0,  c,  0,
      0,  0,  0,  1,
   };

   return(result);
}

function matrix4 rotation_z_matrix4(float turns)
{
   float s = sine(turns);
   float c = cosine(turns);

   matrix4 result =
   {
      c,  s,  0,  0,
     -s,  c,  0,  0,
      0,  0,  1,  0,
      0,  0,  0,  1,
   };

   return(result);
}

function matrix4 rotation_matrix4(v3 axis, float turns)
{
   float s = sine(turns);
   float c = cosine(turns);

   v3 a = noz3(axis);
   matrix4 result;

   result.matrix[0][0] = a.x * a.x + (1 - a.x * a.x) * c;
   result.matrix[0][1] = a.x * a.y * (1 - c) - a.z * s;
   result.matrix[0][2] = a.x * a.z * (1 - c) + a.y * s;
   result.matrix[0][3] = 0;

   result.matrix[1][0] = a.x * a.y * (1 - c) + a.z * s;
   result.matrix[1][1] = a.y * a.y + (1 - a.y * a.y) * c;
   result.matrix[1][2] = a.y * a.z * (1 - c) - a.x * s;
   result.matrix[1][3] = 0;

   result.matrix[2][0] = a.x * a.z * (1 - c) - a.y * s;
   result.matrix[2][1] = a.y * a.z * (1 - c) + a.x * s;
   result.matrix[2][2] = a.z * a.z + (1 - a.z * a.z) * c;
   result.matrix[2][3] = 0;

   return(result);
}

function v3 transform3(v3 vector, matrix4 transform)
{
   v3 result;

   result.x = (vector.x * transform.matrix[0][0]) + (vector.y * transform.matrix[0][1]) + (vector.z * transform.matrix[0][2]);
   result.y = (vector.x * transform.matrix[1][0]) + (vector.y * transform.matrix[1][1]) + (vector.z * transform.matrix[1][2]);
   result.z = (vector.x * transform.matrix[2][0]) + (vector.y * transform.matrix[2][1]) + (vector.z * transform.matrix[2][2]);

   return(result);
}

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

   bool up;
   bool down;
   bool left;
   bool right;

   bool move_up;
   bool move_down;
   bool move_left;
   bool move_right;
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

   // NOTE(law): Both camera-space and world-space are represented using
   // right-hand coordinate systems. The camera's y-axis points up relative to
   // its image. It's z-axis points away from the scene, into the camera.

   v3 camera_position;

   v3 camera_x; // right
   v3 camera_y; // up
   v3 camera_z; // negative direction

   float focal_length;

   u32 plane_count;
   struct plane planes[32];
} scene;

function void
point_camera(v3 camera_position, v3 target_position, v3 up)
{
   scene.camera_position = camera_position;

   scene.camera_z = noz3(sub3(camera_position, target_position));
   scene.camera_x = noz3(cross3(noz3(up), scene.camera_z));
   scene.camera_y = cross3(scene.camera_z, scene.camera_x);
}

function void
update(struct render_bitmap *bitmap, struct user_input *input, float frame_seconds_elapsed)
{
   v3 initial_camera_position = {0, 15.0f, 1.5f};
   v3 initial_target_position = {0, 0, 1.5f};
   v3 initial_up = {0, 0, 1};
   float initial_focal_length = 1.0f;

   if(!scene.is_initialized)
   {
      point_camera(initial_camera_position, initial_target_position, initial_up);
      scene.focal_length = initial_focal_length;

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
      p->color = vec3(0, 0, 1);

      scene.is_initialized = true;
   }

   // NOTE(law): Handle user input.
   if(input->function_keys[1])
   {
      // NOTE(law): Reset camera.
      point_camera(initial_camera_position, initial_target_position, initial_up);
      scene.focal_length = initial_focal_length;
   }
   else
   {
      if(input->control_scroll)
      {
         scene.focal_length += (input->scroll_delta * 0.25f);
      }

      float pitch_turns = 0;
      float yaw_turns = 0;
      float turn_increment = 0.01f;

      if(input->move_up)
      {
         scene.camera_position = sub3(scene.camera_position, mul3(scene.camera_z, 0.25f));
      }
      if(input->move_down)
      {
         scene.camera_position = add3(scene.camera_position, mul3(scene.camera_z, 0.25f));
      }
      if(input->move_left)
      {
         scene.camera_position = sub3(scene.camera_position, mul3(scene.camera_x, 0.25f));
      }
      if(input->move_right)
      {
         scene.camera_position = add3(scene.camera_position, mul3(scene.camera_x, 0.25f));
      }

      if(input->up)
      {
         pitch_turns += turn_increment;
      }
      if(input->down)
      {
         pitch_turns -= turn_increment;
      }
      if(input->left)
      {
         yaw_turns -= turn_increment;
      }
      if(input->right)
      {
         yaw_turns += turn_increment;
      }

      if(pitch_turns)
      {
         matrix4 rotation_pitch = rotation_matrix4(scene.camera_x, pitch_turns);
         scene.camera_x = noz3(transform3(scene.camera_x, rotation_pitch));
         scene.camera_y = noz3(transform3(scene.camera_y, rotation_pitch));
         scene.camera_z = noz3(transform3(scene.camera_z, rotation_pitch));
      }

      if(yaw_turns)
      {
         matrix4 rotation_yaw = rotation_z_matrix4(yaw_turns);
         scene.camera_x = noz3(transform3(scene.camera_x, rotation_yaw));
         scene.camera_y = noz3(transform3(scene.camera_y, rotation_yaw));
         scene.camera_z = noz3(transform3(scene.camera_z, rotation_yaw));
      }
   }

   u32 bitmap_width = bitmap->width;
   u32 bitmap_height = bitmap->height;
   float aspect_ratio = (float)bitmap_width / (float)bitmap_height;

   float film_width = 1.0f;
   float film_height = 1.0f / aspect_ratio;
   v3 film_center = sub3(scene.camera_position, mul3(scene.camera_z, scene.focal_length));

   // NOTE(law): Draw into bitmap.
   for(u32 y = 0; y < bitmap_height; ++y)
   {
      float film_v = -1.0f + (2.0f * ((float)y / (float)bitmap_height));

      for(u32 x = 0; x < bitmap_width; ++x)
      {
         float film_u = -1.0f + (2.0f * ((float)x / (float)bitmap_width));

         v3 film_position = film_center;
         film_position = add3(film_position, mul3(scene.camera_x, film_u * 0.5f * film_width));
         film_position = add3(film_position, mul3(scene.camera_y, film_v * 0.5f * film_height));

         v3 ray_direction = noz3(sub3(film_position, scene.camera_position));

         float t_minimum = FLT_MAX;
         struct plane *t_plane = 0;
         for(u32 plane_index = 0; plane_index < scene.plane_count; ++plane_index)
         {
            struct plane *p = scene.planes + plane_index;

            float denominator = dot3(p->normal, ray_direction);
            if(absolute_value(denominator) > 0.0001f)
            {
               float t = (-p->distance - dot3(p->normal, scene.camera_position)) / denominator;
               if(t > 0 && t < t_minimum)
               {
                  t_minimum = t;
                  t_plane = p;
               }
            }
         }

         v3 ray_color = {0, 1, 1};
         if(t_plane)
         {
            float t = dot3(ray_direction, mul3(t_plane->normal, -1.0f));
            ray_color = lerp3(vec3(0.3f, 0.8f, 0.8f), t, t_plane->color);
         }

         u8 r = (u8)(ray_color.r * 255.0f);
         u8 g = (u8)(ray_color.g * 255.0f);
         u8 b = (u8)(ray_color.b * 255.0f);
         u8 a = 255;

         bitmap->memory[(y * bitmap_width) + x] = (r << 16) | (g <<  8) | (b <<  0) | (a << 24);
      }
   }
}
