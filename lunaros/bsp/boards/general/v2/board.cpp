#include "bsp/boards/general/v2.h"
#include "boards/general/v2/resources/descriptor.h"
#include "sdkconfig.h"

#include "boards/general/v2/configuration.h"

#ifndef CONFIG_LUNAROS_BSP_V2_I2C_CLOCK_HZ
#define CONFIG_LUNAROS_BSP_V2_I2C_CLOCK_HZ 400000
#endif
#ifndef CONFIG_LUNAROS_BSP_V2_INTERFACE_I2C_CLOCK_HZ
#define CONFIG_LUNAROS_BSP_V2_INTERFACE_I2C_CLOCK_HZ 400000
#endif

namespace {
constexpr uint64_t bit(int pin) { return uint64_t{1} << pin; }
constexpr bsp_v2_display_t display_mode() {
#if CONFIG_LUNAROS_BSP_V2_DISPLAY_QSPI
  return bsp_v2_display_t::amoled_qspi;
#elif CONFIG_LUNAROS_BSP_V2_DISPLAY_LCD
  return bsp_v2_display_t::lcd_spi;
#else
  return bsp_v2_display_t::none;
#endif
}
constexpr bool has_tf() {
#if CONFIG_LUNAROS_BSP_V2_TFCARD
  return true;
#else
  return false;
#endif
}
constexpr bool has_camera() {
#if CONFIG_LUNAROS_BSP_V2_CAMERA
  return true;
#else
  return false;
#endif
}
constexpr bsp_v2_pin_t pin(int gpio, const char *role, uint32_t constraints = 0) {
  return {gpio, role, gpio >= 1 && gpio <= 10 ? 1 : gpio >= 11 && gpio <= 20 ? 2 : 0,
          gpio >= 1 && gpio <= 10 ? gpio - 1 : gpio >= 11 && gpio <= 20 ? gpio - 11 : -1,
          gpio >= 1 && gpio <= 14 ? gpio : -1, gpio <= 21, constraints};
}
constexpr bsp_v2_pin_t pins[] = {
  pin(4,"a.io1",BSP_V2_PIN_CAMERA_SHARED), pin(5,"a.io2",BSP_V2_PIN_CAMERA_SHARED),
  pin(6,"a.io3",BSP_V2_PIN_CAMERA_SHARED), pin(7,"a.servo",BSP_V2_PIN_CAMERA_SHARED),
  pin(8,"b.io1",BSP_V2_PIN_CAMERA_SHARED), pin(9,"b.io2",BSP_V2_PIN_CAMERA_SHARED),
  pin(10,"b.io3",BSP_V2_PIN_CAMERA_SHARED), pin(3,"b.servo",BSP_V2_PIN_CAMERA_SHARED|BSP_V2_PIN_JTAG_STRAP),
  pin(11,"c.xclk",BSP_V2_PIN_CAMERA_SHARED), pin(12,"c.pclk",BSP_V2_PIN_CAMERA_SHARED),
  pin(13,"c.href",BSP_V2_PIN_CAMERA_SHARED), pin(14,"c.vsync",BSP_V2_PIN_CAMERA_SHARED),
  pin(38,"audio.bclk"), pin(39,"audio.ws"), pin(40,"audio.dout"), pin(41,"audio.din"),
  pin(15,"interface.sda"), pin(16,"interface.scl"),
  pin(18,"interface.d1_miso",BSP_V2_PIN_DISPLAY_CARD_SHARED), pin(17,"interface.te",BSP_V2_PIN_TE_OUTPUT),
  pin(46,"restricted.rgb",BSP_V2_PIN_RGB_CONNECTED|BSP_V2_PIN_DOWNLOAD_STRAP),
  pin(0,"restricted.boot",BSP_V2_PIN_BOOT_BUTTON),
  pin(1,"restricted.sda",BSP_V2_PIN_I2C_CONNECTED), pin(2,"restricted.scl",BSP_V2_PIN_I2C_CONNECTED),
  pin(48,"screen.clk"), pin(44,"screen.cs"), pin(47,"screen.d0"),
  pin(43,"screen.d2_dc",BSP_V2_PIN_ROM_OUTPUT), pin(45,"screen.d3_blk",BSP_V2_PIN_VOLTAGE_STRAP),
  pin(21,"screen.touch_int"), pin(42,"tf.cs"),
  pin(19,"usb.minus",BSP_V2_PIN_USB_EXCLUSIVE), pin(20,"usb.plus",BSP_V2_PIN_USB_EXCLUSIVE),
};
constexpr uint64_t unique_pins() {
  uint64_t mask = 0;
  for (const auto &p : pins) { if (mask & bit(p.gpio)) return 0; mask |= bit(p.gpio); }
  return mask;
}
static_assert(sizeof(pins)/sizeof(pins[0]) == 33);
static_assert(unique_pins() == ((bit(22)-1) | ((bit(49)-1) & ~(bit(38)-1))));
constexpr bsp_v2_group_t groups[] = {
  {"A",{4,5,6,7},4,7,true,"3 IO + IO/V/G; optional three-servo matrix"},
  {"B",{8,9,10,3},4,3,true,"3 IO + IO/V/G; GPIO3 JTAG strap"},
  {"C",{11,12,13,14},4,14,true,"3 IO + IO/V/G; ADC2/touch"},
  {"audio",{38,39,40,41},4,41,true,"3 IO + IO/V/G; independent I2S"},
  {"interface",{15,16,18,17},4,-1,true,"two 2 IO/V/G four-pin ports"},
  {"restricted",{46,0,1,2},4,-1,false,"not exposed; RGB/BOOT/onboard I2C"},
  {"screen",{48,44,47,43,45,21},6,-1,true,"LCD or QSPI, including touch IRQ"},
  {"tf",{42},1,-1,true,"CS only; shares SPI signals"},
  {"usb",{19,20},2,-1,true,"exclusive USB D-/D+"},
};
const bsp_v2_board_t &descriptor() {
  static const bsp_v2_board_t board = [] {
  bsp_v2_board_t b{};
  b.revision="v2"; b.support_status="prototype";
  b.pins=pins; b.pin_count=33; b.groups=groups; b.group_count=9;
  b.common_i2c={"v2.common",1,2,CONFIG_LUNAROS_BSP_V2_I2C_CLOCK_HZ,BSP_V2_RESOURCE_COMMON_I2C};
  b.interface_i2c={"v2.interface",15,16,CONFIG_LUNAROS_BSP_V2_INTERFACE_I2C_CLOCK_HZ,BSP_V2_RESOURCE_INTERFACE_I2C};
  const auto mode=display_mode(); const bool qspi=mode==bsp_v2_display_t::amoled_qspi;
  b.spi_available=mode!=bsp_v2_display_t::none || has_tf();
  b.spi={"v2.display_tf",static_cast<spi_host_device_t>(1),47,qspi||has_tf()?18:-1,48,65536,BSP_V2_RESOURCE_SPI_BUS};
  b.screen={mode,mode==bsp_v2_display_t::none?-1:44,mode==bsp_v2_display_t::none?-1:43,
            mode==bsp_v2_display_t::none?-1:45,qspi?17:-1,mode==bsp_v2_display_t::none?-1:21,40000000,-1};
  b.audio={38,39,40,41,-1};
  b.camera.pin_xclk=11; b.camera.pin_pclk=12; b.camera.pin_href=13; b.camera.pin_vsync=14;
  b.camera.pin_sccb_sda=1; b.camera.pin_sccb_scl=2;
  b.camera.pin_d0=4; b.camera.pin_d1=5; b.camera.pin_d2=6; b.camera.pin_d3=7;
  b.camera.pin_d4=8; b.camera.pin_d5=9; b.camera.pin_d6=10; b.camera.pin_d7=3;
  // Timing must be supplied after the actual camera module is selected.
  b.camera.xclk_freq_hz=0; b.camera_enabled=has_camera();
  b.tf_enabled=has_tf(); b.tf_cs=42; b.tf_clock_hz=20000000;
  b.rgb=46; b.boot=0; b.usb_minus=19; b.usb_plus=20;
  return b;
}();
  return board;
}
}

const bsp_v2_board_t *bsp_v2_board() {
  #if CONFIG_LUNAROS_BSP_V2_ENABLED
  return &descriptor();
#else
  return nullptr;
#endif
}
esp_err_t bsp_v2_camera_config(bsp_v2_camera_t *out) {
  if (!out) return ESP_ERR_INVALID_ARG;
  *out={};
  const auto *b=bsp_v2_board();
  if (!b || !b->camera_enabled) return ESP_ERR_NOT_SUPPORTED;
  *out=b->camera;
  return ESP_OK;
}

namespace lunaredu::bsp_v2 {
void fill_resources(ResourceTable &table) {
  const auto &board=descriptor();
  const auto set=[&](bsp_v2_resource_t id,uint64_t mask,bool available=true) {
    table[id].pin_mask=mask; table[id].available=available;
  };
  set(BSP_V2_RESOURCE_ZONE_A,bit(4)|bit(5)|bit(6)|bit(7));
  set(BSP_V2_RESOURCE_ZONE_B,bit(8)|bit(9)|bit(10)|bit(3));
  set(BSP_V2_RESOURCE_ZONE_C,bit(11)|bit(12)|bit(13)|bit(14));
  set(BSP_V2_RESOURCE_AUDIO,bit(38)|bit(39)|bit(40)|bit(41));
  set(BSP_V2_RESOURCE_COMMON_I2C,bit(1)|bit(2));
  set(BSP_V2_RESOURCE_INTERFACE_I2C,bit(15)|bit(16));
  const bool qspi=board.screen.mode==bsp_v2_display_t::amoled_qspi;
  // TE is driven even when disabled; declaration must not advertise it as a
  // free output on an AMOLED assembly. TF consumes MISO even on an LCD board.
  const uint64_t aux=qspi?0:bit(17)|(board.tf_enabled?0:bit(18));
  set(BSP_V2_RESOURCE_INTERFACE_AUX,aux,aux!=0);
  set(BSP_V2_RESOURCE_SPI_BUS,bit(47)|bit(48)|(board.spi.miso_pin>=0?bit(18):0)|
      (qspi?bit(43)|bit(45):0),board.spi_available);
  set(BSP_V2_RESOURCE_SCREEN,bit(44)|(qspi?bit(17):bit(43)|bit(45)),
      board.screen.mode!=bsp_v2_display_t::none);
  set(BSP_V2_RESOURCE_TOUCH_INT,bit(21),board.screen.mode!=bsp_v2_display_t::none);
  set(BSP_V2_RESOURCE_BOOT,bit(0));
  set(BSP_V2_RESOURCE_RGB,bit(46));
  set(BSP_V2_RESOURCE_USB,bit(19)|bit(20));
  // SCCB is borrowed from the common bus, not an exclusive camera resource.
  set(BSP_V2_RESOURCE_CAMERA,bit(3)|bit(4)|bit(5)|bit(6)|bit(7)|bit(8)|bit(9)|bit(10)|
      bit(11)|bit(12)|bit(13)|bit(14),board.camera_enabled);
  // CS resource only; display and card share the separately owned SPI host.
  set(BSP_V2_RESOURCE_TFCARD,bit(42),board.tf_enabled);
}

size_t resource_signals(bsp_v2_resource_t resource,bsp_v2_signal_t *out,size_t capacity) {
  const auto &b=descriptor();size_t count=0;
  const auto add=[&](const char *role,int p) {
    if(p<0 || !bsp_v2_resource_contains_pin(resource,p))return;
    if(out && count<capacity)out[count]={role,p};
    ++count;
  };
  switch(resource) {
  case BSP_V2_RESOURCE_ZONE_A: case BSP_V2_RESOURCE_ZONE_B: case BSP_V2_RESOURCE_ZONE_C: {
    const int i=static_cast<int>(resource)-BSP_V2_RESOURCE_ZONE_A;
    constexpr const char *roles[]={"io1","io2","io3","direct_servo"};
    for(int j=0;j<4;++j) add(roles[j],groups[i].pins[j]);
    break;
  }
  case BSP_V2_RESOURCE_AUDIO:
    add("bclk",38);add("word_select",39);add("data_out",40);add("data_in",41);break;
  case BSP_V2_RESOURCE_COMMON_I2C: add("sda",1);add("scl",2);break;
  case BSP_V2_RESOURCE_INTERFACE_I2C: add("sda",15);add("scl",16);break;
  case BSP_V2_RESOURCE_INTERFACE_AUX: add("te_or_io",17);add("miso_or_io",18);break;
  case BSP_V2_RESOURCE_SPI_BUS:
    add("clock",48);add("data0_mosi",47);add("data1_miso",b.spi.miso_pin);
    if(b.screen.mode==bsp_v2_display_t::amoled_qspi){add("data2",43);add("data3",45);}break;
  case BSP_V2_RESOURCE_SCREEN:
    add("chip_select",44);
    if(b.screen.mode==bsp_v2_display_t::amoled_qspi)add("te",17);
    else {add("data_command",43);add("backlight",45);}break;
  case BSP_V2_RESOURCE_TOUCH_INT: add("interrupt",21);break;
  case BSP_V2_RESOURCE_BOOT: add("button",0);break;
  case BSP_V2_RESOURCE_RGB: add("digital_data",46);break;
  case BSP_V2_RESOURCE_TFCARD: add("chip_select",42);break;
  case BSP_V2_RESOURCE_USB: add("data_minus",19);add("data_plus",20);break;
  case BSP_V2_RESOURCE_CAMERA: {
    constexpr const char *roles[]={"data0","data1","data2","data3","data4","data5","data6","data7"};
    constexpr int data[]={4,5,6,7,8,9,10,3};
    for(int i=0;i<8;++i)add(roles[i],data[i]);
    add("xclk",11);add("pclk",12);add("href",13);add("vsync",14);break;
  }
  default:break;
  }
  return count;
}
}
