"""Run the real RGB565 flush under a 4 KiB DMA budget, checking every pixel."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[1]
s=(root/'components_device/display/lunaredu_device_spi_lcd/src/device_spi_lcd_surface.cpp').read_text()
start=s.index('bool device_spi_lcd_surface_draw_rgb565_bitmap(')
function=s[start:s.index('\nbool device_spi_lcd_surface_draw_rgb666_bitmap(',start)]
code=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
using esp_err_t=int;constexpr int ESP_OK=0;
enum class DeviceSpiLcdController {kIli9488,kSt7789};
enum class DeviceSpiLcdTransferMode {kWaitForCompletion};
std::recursive_mutex g_mutex;
struct {uint16_t width=240,height=240,offset_x=0,offset_y=0;void *panel=nullptr;bool rgb565_panel_byte_swap=false;DeviceSpiLcdController controller=DeviceSpiLcdController::kSt7789;}g_panel;
uint16_t *g_panel_dma_pixels=nullptr;size_t g_panel_dma_pixel_capacity=0;std::vector<uint16_t> buffer;
unsigned g_frame_count=0;std::string g_last_pattern;int calls=0,allocations=0,waits=0;bool fail_transfer=false;
std::vector<uint16_t> screen(240*240),mirror(240*240);
bool ensure_panel_initialized_locked(){return true;}
void set_last_error(const std::string&){}void copy_error_out(std::string*){}void clear_error_out(std::string*){}
const char *esp_err_to_name(int){return "error";}
uint16_t swap_rgb565_bytes(uint16_t p){return (p>>8)|(p<<8);}
uint16_t panel_rgb565(uint16_t p){return p;}
bool ensure_panel_dma_buffer(size_t count,bool=true){
 if(count<=g_panel_dma_pixel_capacity)return true;
 ++allocations;buffer.clear();g_panel_dma_pixels=nullptr;g_panel_dma_pixel_capacity=0;
 if(count*2>4096)return false;
 buffer.resize(count);g_panel_dma_pixels=buffer.data();g_panel_dma_pixel_capacity=count;return true;
}
int esp_lcd_panel_draw_bitmap(void*,int x,int y,int x2,int y2,const void *pixels){
 ++calls;if(fail_transfer)return -1;
 auto *p=static_cast<const uint16_t*>(pixels);
 for(int j=y;j<y2;++j)for(int i=x;i<x2;++i)screen[j*240+i]=*p++;
 return 0;
}
bool wait_for_panel_transfer(const char*){++waits;return true;}
void mirror_bitmap(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t*p){
 for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)mirror[j*240+i]=*p++;
}
''' + function + r'''
int main(){
 std::vector<uint16_t> pixels(240*32);for(unsigned i=0;i<pixels.size();++i)pixels[i]=i;
 assert(device_spi_lcd_surface_draw_rgb565_bitmap(0,0,240,32,pixels.data(),false,nullptr,DeviceSpiLcdTransferMode::kWaitForCompletion));
 assert(calls<=4 && "DMA pressure must not turn a 32-row tile into 32 SPI transfers");
 assert(std::equal(pixels.begin(),pixels.end(),screen.begin()));assert(screen==mirror);
 const int allocated=allocations;calls=0;
 assert(device_spi_lcd_surface_draw_rgb565_bitmap(32,32,176,31,pixels.data(),true,nullptr,DeviceSpiLcdTransferMode::kWaitForCompletion));
 for(int y=0;y<31;++y)for(int x=0;x<176;++x){auto want=swap_rgb565_bytes(pixels[y*176+x]);assert(screen[(y+32)*240+x+32]==want);}
 assert(screen==mirror && allocations==allocated && calls<=4);
 fail_transfer=true;assert(!device_spi_lcd_surface_draw_rgb565_bitmap(0,0,240,32,pixels.data(),false,nullptr,DeviceSpiLcdTransferMode::kWaitForCompletion));
 assert(!device_spi_lcd_surface_draw_rgb565_bitmap(230,0,240,32,pixels.data(),false,nullptr,DeviceSpiLcdTransferMode::kWaitForCompletion));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('LCD bounded DMA strips: constrained heap, exact pixels, byte swap, reuse and failure passed')
