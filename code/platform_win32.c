/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <windows.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "raw.c"

#define WIN32_SECONDS_ELAPSED(start, end) ((float)((end).QuadPart - (start).QuadPart) \
      / (float)win32_global_counts_per_second.QuadPart)

global bool win32_global_is_running;
global LARGE_INTEGER win32_global_counts_per_second;
global BITMAPINFO *win32_global_bitmap_info;
global struct render_bitmap *win32_global_bitmap;
global WINDOWPLACEMENT win32_global_previous_window_placement =
{
   sizeof(win32_global_previous_window_placement)
};

#define WIN32_DEFAULT_DPI 96
global int win32_global_dpi = WIN32_DEFAULT_DPI;

#define WIN32_LOG_MAX_LENGTH 1024

function
PLATFORM_LOG(platform_log)
{
   char message[WIN32_LOG_MAX_LENGTH];

   va_list arguments;
   va_start(arguments, format);
   {
      vsnprintf(message, sizeof(message), format, arguments);
   }
   va_end(arguments);

   OutputDebugStringA(message);
}

function void *
win32_allocate(SIZE_T size)
{
   void *result = VirtualAlloc(0, size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
   return(result);
}

function void
win32_deallocate(void *memory)
{
   if(!VirtualFree(memory, 0, MEM_RELEASE))
   {
      platform_log("ERROR: Failed to free virtual memory.\n");
   }
}

function void
win32_display_bitmap(struct render_bitmap bitmap, HWND window, HDC device_context)
{
   RECT client_rect;
   GetClientRect(window, &client_rect);

   s32 client_width = client_rect.right - client_rect.left;
   s32 client_height = client_rect.bottom - client_rect.top;

   u32 toolbar_height = 0;
   client_height -= toolbar_height;

   u32 status_height = 0;
   client_height -= status_height;

   float client_aspect_ratio = (float)client_width / (float)client_height;
   float target_aspect_ratio = (float)RESOLUTION_BASE_WIDTH / (float)RESOLUTION_BASE_HEIGHT;

   float target_width  = (float)client_width;
   float target_height = (float)client_height;
   float gutter_width  = 0;
   float gutter_height = 0;

   if(client_aspect_ratio > target_aspect_ratio)
   {
      // NOTE(law): The window is too wide, fill in the left and right sides
      // with black gutters.
      target_width = target_aspect_ratio * client_height;
      gutter_width = (client_width - target_width) / 2;
   }
   else if(client_aspect_ratio < target_aspect_ratio)
   {
      // NOTE(law): The window is too tall, fill in the top and bottom with
      // black gutters.
      target_height = (1.0f / target_aspect_ratio) * client_width;
      gutter_height = (client_height - target_height) / 2;
   }

   if(client_aspect_ratio > target_aspect_ratio)
   {
      // NOTE(law): The window is too wide, fill in the left and right sides
      // with black gutters.
      PatBlt(device_context, 0, toolbar_height, (int)gutter_width, (int)target_height, BLACKNESS);
      PatBlt(device_context, (int)(client_width - gutter_width), toolbar_height, (int)gutter_width, (int)target_height, BLACKNESS);
   }
   else if(client_aspect_ratio < target_aspect_ratio)
   {
      // NOTE(law): The window is too tall, fill in the top and bottom with
      // black gutters.
      PatBlt(device_context, 0, toolbar_height, (int)target_width, (int)gutter_height, BLACKNESS);
      PatBlt(device_context, 0, toolbar_height + (int)(client_height - gutter_height), (int)target_width, (int)gutter_height, BLACKNESS);
   }

   int target_x = (int)gutter_width;
   int target_y = (int)(gutter_height + toolbar_height);

   StretchDIBits(device_context,
                 target_x, target_y, (int)target_width, (int)target_height, // Destination
                 0, 0, bitmap.width, bitmap.height, // Source
                 bitmap.memory, win32_global_bitmap_info, DIB_RGB_COLORS, SRCCOPY);
}

function void
win32_toggle_fullscreen(HWND window)
{
   // NOTE(law): Based on version by Raymond Chen:
   // https://devblogs.microsoft.com/oldnewthing/20100412-00/?p=14353

   // TODO(law): Check what this does with multiple monitors.
   DWORD style = GetWindowLong(window, GWL_STYLE);
   if(style & WS_OVERLAPPEDWINDOW)
   {
      MONITORINFO monitor_info = {sizeof(monitor_info)};

      if (GetWindowPlacement(window, &win32_global_previous_window_placement) &&
          GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor_info))
      {
         int x = monitor_info.rcMonitor.left;
         int y = monitor_info.rcMonitor.top;
         int width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
         int height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;

         SetWindowLong(window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
         SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
      }
   }
   else
   {
      SetWindowLong(window, GWL_STYLE, style|WS_OVERLAPPEDWINDOW);
      SetWindowPlacement(window, &win32_global_previous_window_placement);
      SetWindowPos(window, 0, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
   }
}

function bool
win32_is_fullscreen(HWND window)
{
   DWORD style = GetWindowLong(window, GWL_STYLE);

   bool result = !(style & WS_OVERLAPPEDWINDOW);
   return(result);
}

function void
win32_adjust_window_rect(RECT *window_rect)
{
   bool dpi_supported = false;
   bool window_has_menu = false;
   DWORD window_style = WS_OVERLAPPEDWINDOW;

   // NOTE(law): Try to use the Windows 10 API for a DPI-aware window adjustment.
   HMODULE library = LoadLibrary(TEXT("user32.dll"));
   if(library)
   {
      // TODO(law) Cache the function pointer so the library doesn't need to be
      // reloaded on every resolution update.

      typedef BOOL Function(LPRECT, DWORD, BOOL, DWORD, UINT);
      Function *AdjustWindowRectExForDpi = (Function *)GetProcAddress(library, "AdjustWindowRectExForDpi");
      if(AdjustWindowRectExForDpi)
      {
         AdjustWindowRectExForDpi(window_rect, window_style, window_has_menu, 0, win32_global_dpi);
         dpi_supported = true;
      }

      FreeLibrary(library);
   }

   if(!dpi_supported)
   {
      AdjustWindowRect(window_rect, window_style, window_has_menu);
   }
}

function void
win32_set_window_size(HWND window, u32 client_width, u32 client_height)
{
   if(!win32_is_fullscreen(window))
   {
      RECT window_rect = {0};
      window_rect.right  = client_width;
      window_rect.bottom = client_height;

      if(win32_global_dpi > WIN32_DEFAULT_DPI)
      {
         window_rect.right  *= 2;
         window_rect.bottom *= 2;
      }

      win32_adjust_window_rect(&window_rect);

      u32 window_width  = window_rect.right - window_rect.left;
      u32 window_height = window_rect.bottom - window_rect.top;

      SetWindowPos(window, 0, 0, 0, window_width, window_height, SWP_NOMOVE);
   }
}

function int
win32_get_window_dpi(HWND window)
{
   int result = 0;

   // NOTE(law): Try to use the Windows 8.1 API to get the monitor's DPI.
   HMODULE library = LoadLibrary(TEXT("shcore.lib"));
   if(library)
   {
      typedef HRESULT Function(HMONITOR, int, UINT *, UINT *);
      Function *GetDpiForMonitor = (Function *)GetProcAddress(library, "GetDpiForMonitor");

      if(GetDpiForMonitor)
      {
         HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);

         UINT dpi_x, dpi_y;
         if(SUCCEEDED(GetDpiForMonitor(monitor, 0, &dpi_x, &dpi_y)))
         {
            result = dpi_x;
         }
      }

      FreeLibrary(library);
   }

   if(!result)
   {
      // NOTE(law): If we don't have access to the Windows 8.1 API, just grab the
      // DPI off the primary monitor.
      HDC device_context = GetDC(0);
      result = GetDeviceCaps(device_context, LOGPIXELSX);
      ReleaseDC(0, device_context);
   }
   assert(result);

   return(result);
}

function void
win32_process_input(struct user_input *input, HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   bool alt_key_pressed = lparam & (1 << 29);
   bool key_previously_down = lparam & (1 << 30);

   if(!key_previously_down)
   {
      if(wparam == VK_ESCAPE || (alt_key_pressed && wparam == VK_F4))
      {
         win32_global_is_running = false;
      }
      else if(wparam == 'F' || (alt_key_pressed && wparam == VK_RETURN))
      {
         win32_toggle_fullscreen(window);
      }
      else if(wparam == '1')
      {
         win32_set_window_size(window, RESOLUTION_BASE_WIDTH, RESOLUTION_BASE_HEIGHT);
      }
      else if(wparam == '2')
      {
         win32_set_window_size(window, 2*RESOLUTION_BASE_WIDTH, 2*RESOLUTION_BASE_HEIGHT);
      }
      else if(wparam == VK_F1)
      {
         input->function_keys[1] = true;
      }
      else if(wparam == VK_F2)
      {
         input->function_keys[2] = true;
      }
      else if(wparam == VK_F3)
      {
         input->function_keys[3] = true;
      }
      else if(wparam == VK_F4)
      {
         input->function_keys[4] = true;
      }
      else if(wparam == VK_F5)
      {
         input->function_keys[5] = true;
      }
      else if(wparam == VK_F6)
      {
         input->function_keys[6] = true;
      }
      else if(wparam == VK_F7)
      {
         input->function_keys[7] = true;
      }
      else if(wparam == VK_F8)
      {
         input->function_keys[8] = true;
      }
      else if(wparam == VK_F9)
      {
         input->function_keys[9] = true;
      }
      else if(wparam == VK_F10)
      {
         input->function_keys[10] = true;
      }
      else if(wparam == VK_F11)
      {
         input->function_keys[11] = true;
      }
      else if(wparam == VK_F12)
      {
         input->function_keys[12] = true;
      }
   }

   // Mouse handling:
   if(message == WM_LBUTTONUP || message == WM_LBUTTONDOWN)
   {
      input->mouse_left = (message != WM_LBUTTONUP);
   }
   else if(message == WM_MBUTTONUP || message == WM_MBUTTONDOWN)
   {
      input->mouse_middle = (message != WM_MBUTTONUP);
   }
   else if(message == WM_RBUTTONUP || message == WM_RBUTTONDOWN)
   {
      input->mouse_right = (message != WM_RBUTTONUP);
   }
   else if(message == WM_MOUSEWHEEL)
   {
      input->control_scroll = GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL;
      input->scroll_delta = GET_WHEEL_DELTA_WPARAM(wparam) / (float)WHEEL_DELTA;
   }
}

LRESULT
win32_window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   LRESULT result = 0;

   switch(message)
   {
      case WM_CLOSE:
      {
         DestroyWindow(window);
      } break;

      case WM_CREATE:
      {
         win32_global_dpi = win32_get_window_dpi(window);
         win32_set_window_size(window, RESOLUTION_BASE_WIDTH, RESOLUTION_BASE_HEIGHT);
      } break;

      case WM_DESTROY:
      {
         win32_global_is_running = false;
         PostQuitMessage(0);
      } break;

      case WM_DPICHANGED:
      {
         win32_global_dpi = HIWORD(wparam);

         RECT *updated_window = (RECT *)lparam;
         int x = updated_window->left;
         int y = updated_window->top;
         int width = updated_window->right - updated_window->left;
         int height = updated_window->bottom - updated_window->top;

         SetWindowPos(window, 0, x, y, width, height, SWP_NOZORDER|SWP_NOACTIVATE);
      } break;

      // TODO(law): Add real keyboard support!
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
      {
         assert(!"Input is no longer handled in the window callback.");
      } break;

      case WM_PAINT:
      {
         PAINTSTRUCT paint;
         HDC device_context = BeginPaint(window, &paint);
         win32_display_bitmap(*win32_global_bitmap, window, device_context);
         ReleaseDC(window, device_context);
      } break;

      default:
      {
         result = DefWindowProc(window, message, wparam, lparam);
      } break;
   }

   return(result);
}

INT WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous_instance, LPSTR command_line, INT show_command)
{
   QueryPerformanceFrequency(&win32_global_counts_per_second);
   bool sleep_is_granular = (timeBeginPeriod(1) == TIMERR_NOERROR);

   WNDCLASSEX window_class = {0};
   window_class.cbSize = sizeof(window_class);
   window_class.style = CS_HREDRAW|CS_VREDRAW;
   window_class.lpfnWndProc = win32_window_callback;
   window_class.hInstance = instance;
   window_class.hIcon = LoadIcon(0, IDI_APPLICATION);
   window_class.hCursor = LoadCursor(0, IDC_ARROW);
   window_class.lpszClassName = TEXT("RAW Software Renderer");

   if(!RegisterClassEx(&window_class))
   {
      platform_log("ERROR: Failed to register a window class.\n");
      return(1);
   }

   DWORD window_style = WS_OVERLAPPEDWINDOW;

   HWND window = CreateWindowEx(0,
                                window_class.lpszClassName,
                                TEXT("RAW Software Renderer"),
                                window_style,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                0,
                                0,
                                instance,
                                0);

   if(!window)
   {
      platform_log("ERROR: Failed to create a window.\n");
      return(1);
   }

   // NOTE(law) Set up the rendering bitmap.
   struct render_bitmap bitmap = {RESOLUTION_BASE_WIDTH, RESOLUTION_BASE_HEIGHT};

   SIZE_T bytes_per_pixel = sizeof(u32);
   SIZE_T bitmap_size = bitmap.width * bitmap.height * bytes_per_pixel;
   bitmap.memory = win32_allocate(bitmap_size);
   if(!bitmap.memory)
   {
      platform_log("ERROR: Windows failed to allocate our bitmap.\n");
      return(1);
   }

   BITMAPINFOHEADER bitmap_header = {0};
   bitmap_header.biSize = sizeof(BITMAPINFOHEADER);
   bitmap_header.biWidth = bitmap.width;
   bitmap_header.biHeight = -(s32)bitmap.height; // NOTE(law): Negative will indicate a top-down bitmap.
   bitmap_header.biPlanes = 1;
   bitmap_header.biBitCount = 32;
   bitmap_header.biCompression = BI_RGB;

   BITMAPINFO bitmap_info = {bitmap_header};

   win32_global_bitmap = &bitmap;
   win32_global_bitmap_info = &bitmap_info;

   // NOTE(law): Display the created window.
   ShowWindow(window, show_command);
   UpdateWindow(window);

   float target_seconds_per_frame = 1.0f / 60.0f;
   float frame_seconds_elapsed = 0;

   LARGE_INTEGER frame_start_count;
   QueryPerformanceCounter(&frame_start_count);

   struct user_input input = {0};

   win32_global_is_running = true;
   while(win32_global_is_running)
   {
      // TODO(law): For now, function keys and mouse scroll capture the initial
      // key press, so they get cleared here every frame. Improve input state
      // management instead.

      input.control_scroll = false;
      input.scroll_delta = 0;
      for(u32 index = 0; index < ARRAY_LENGTH(input.function_keys); ++index)
      {
         input.function_keys[index] = 0;
      }

      MSG message;
      while(PeekMessage(&message, 0, 0, 0, PM_REMOVE))
      {
         if(message.message == WM_KEYUP || message.message == WM_SYSKEYUP ||
            message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN ||
            message.message == WM_LBUTTONUP || message.message == WM_LBUTTONDOWN ||
            message.message == WM_MBUTTONUP || message.message == WM_MBUTTONDOWN ||
            message.message == WM_RBUTTONUP || message.message == WM_RBUTTONDOWN ||
            message.message == WM_MOUSEWHEEL)
         {
            win32_process_input(&input, window, message.message, message.wParam, message.lParam);
         }
         else
         {
            TranslateMessage(&message);
            DispatchMessage(&message);
         }
      }

      POINT cursor_position;
      GetCursorPos(&cursor_position);
      ScreenToClient(window, &cursor_position);
      input.mouse_x = cursor_position.x;
      input.mouse_y = cursor_position.y;

      update(&bitmap, &input, frame_seconds_elapsed);

      // NOTE(law): Blit bitmap to screen.
      HDC device_context = GetDC(window);
      win32_display_bitmap(bitmap, window, device_context);
      ReleaseDC(window, device_context);

      // NOTE(law): Calculate elapsed frame time.
      LARGE_INTEGER frame_end_count;
      QueryPerformanceCounter(&frame_end_count);
      frame_seconds_elapsed = WIN32_SECONDS_ELAPSED(frame_start_count, frame_end_count);

      // NOTE(law): If possible, sleep for some of the remaining frame time. The
      // sleep time calculation intentionally undershoots to prevent
      // oversleeping due to the lack of sub-millisecond granualarity.
      DWORD sleep_ms = 0;
      float sleep_fraction = 0.9f;
      if(sleep_is_granular && (frame_seconds_elapsed < target_seconds_per_frame))
      {
         sleep_ms = (DWORD)((target_seconds_per_frame - frame_seconds_elapsed) * 1000.0f * sleep_fraction);
         if(sleep_ms > 0)
         {
            Sleep(sleep_ms);
         }
      }

      // NOTE(law): Spin lock for the remaining frame time.
      while(frame_seconds_elapsed < target_seconds_per_frame)
      {
         QueryPerformanceCounter(&frame_end_count);
         frame_seconds_elapsed = WIN32_SECONDS_ELAPSED(frame_start_count, frame_end_count);
      }
      frame_start_count = frame_end_count;

      platform_log("Frame time: %0.03fms, ", frame_seconds_elapsed * 1000.0f);
      platform_log("Sleep: %ums\n", sleep_ms);
   }

   return(0);
}
