"""Run the actual raw MPU projection and the retired motion JSON adapter."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

app=Path(__file__).resolve().parents[1]
repo=Path(__file__).resolve().parents[1]
source=(app/'main/smart_speaker/smart_speaker_sensors.cpp').read_text(encoding='utf-8')
header=(app/'main/smart_speaker/smart_speaker_sensors.h').read_text(encoding='utf-8')
projection=(app/'main/smart_speaker/smart_speaker_projection.cpp').read_text(encoding='utf-8')
snapshot=re.search(r'struct SmartSpeakerMpuSnapshot \{.*?\n\};',header,re.S).group()
addresses=source[source.index('std::string format_addresses('):source.index('void scan_mpu_bus_locked(')]
update=source[source.index('void update_mpu_snapshot_locked()'):source.index('bool stop_mpu_locked(')]
motion=projection[projection.index('  JsonObject motion_json='):projection.index('  const auto touch =',projection.index('  JsonObject motion_json='))]
code=r'''
#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
constexpr size_t kSmartSpeakerI2cScanMaxAddresses=16;
''' + snapshot + r'''
SmartSpeakerMpuSnapshot g_mpu;
struct Status {
 bool initialized=true,sampling=true;uint8_t i2c_address=0x68,who_am_i=0x68;
 struct {float temperature_c=27.5f;} latest;
 struct {bool valid=true;float angle_x=10,angle_y=-20,angle_z=30;} pose;
 std::string last_error;
} status;
int ticks=0;
namespace lunaredu::device {
void tick_mpu6050(){++ticks;}
Status mpu6050_status(){return status;}
}
''' + addresses + update + r'''
int main(){
 update_mpu_snapshot_locked();
 assert(ticks==1&&g_mpu.initialized&&g_mpu.sampling&&g_mpu.address==0x68);
 assert(g_mpu.temperature_c==27.5f&&g_mpu.pose_valid&&g_mpu.angle_x==10&&g_mpu.angle_y==-20&&g_mpu.angle_z==30);
 assert(g_mpu.who_am_i_valid&&g_mpu.who_am_i==0x68&&g_mpu.who_am_i_error.empty());
 status.initialized=false;status.pose.valid=false;status.last_error="disconnected";
 g_mpu.scan_complete=true;g_mpu.scan_count=1;g_mpu.scan_addresses[0]=0x68;
 update_mpu_snapshot_locked();assert(ticks==2&&!g_mpu.pose_valid&&!g_mpu.initialized);
 assert(g_mpu.last_error=="disconnected scan=0x68");
 JsonDocument doc;auto payload=doc.to<JsonObject>();
''' + motion + r'''
 assert(payload["motion"].size()==6&&!payload["motion"]["valid"].as<bool>()&&!payload["motion"]["supported"].as<bool>());
 for(const char *key:{"tilt_x","tilt_y","twist_z","angular_speed_dps"})assert(payload["motion"][key].isNull());
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-sensors-') as tmp:
    path=Path(tmp)
    (path/'test.cpp').write_text(code,encoding='utf-8')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-Wall','-Wextra','-Werror',
                   '-I'+str(app/'managed_components/bblanchon__arduinojson/src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Raw MPU identity/pose/error diagnostics preserved; unused synthetic motion explicitly unavailable')
