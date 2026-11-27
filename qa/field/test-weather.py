"""Run the real weather parser/service with bounded HTTP and preference fixtures."""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--forecast", type=Path, required=True)
parser.add_argument("--places", type=Path, required=True)
args = parser.parse_args()
core = Path(os.environ["PLATFORMIO_CORE_DIR"])
cjson = core / "packages/framework-espidf/components/json/cJSON"
source = (root / "src/TDeckWeather.cpp").read_text()
source = source[source.index("namespace meshweather") :]
stub = r"""
#include "util/WeatherParser.h"
#include "util/FieldLocale.h"
static const char *testLocale = "en";
extern "C" const char *lv_i18n_get_current_locale(void) { return testLocale; }
#include "util/HttpBodyBuffer.h"
#include "mesh/Throttle.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>
constexpr int MALLOC_CAP_SPIRAM=1,HTTPC_DISABLE_FOLLOW_REDIRECTS=0,pdPASS=1;
const char *kHearHamRootCA="test-root";
uint32_t nowMs=10000; bool wifi=false,failAllocation=false,failTask=false,failSave=false;
int owner=0,loans=0,takes=0,responseCode=200;
std::string response,lastURL;
void (*queued)(void *)=nullptr;
uint32_t millis() {return nowMs;}
bool Throttle::isWithinTimespanMs(uint32_t last,uint32_t span){return uint32_t(millis()-last)<span;}
extern "C" bool tdeck_wifi_connected(){return wifi;}
extern "C" bool tdeck_network_job_acquire(int id){if(owner)return false;owner=id;return true;}
extern "C" void tdeck_network_job_release(int id){assert(owner==id);owner=0;}
extern "C" void tdeck_tls_reserve_release(){++loans;}
extern "C" void tdeck_tls_reserve_take(){++takes;}
void *heap_caps_malloc(size_t size,int){return failAllocation?nullptr:malloc(size);}
void heap_caps_free(void *p){free(p);}
int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,int,void *){
 if(failTask)return 0;assert(!queued);queued=fn;return pdPASS;
}
void vTaskDelete(void *){}
struct WiFiClientSecure {
 void setCACert(const char *p){assert(p==kHearHamRootCA);}
 void setHandshakeTimeout(int){}
};
struct HTTPClient {
 void setConnectTimeout(int){} void setTimeout(int){} void setReuse(bool){}
 void setFollowRedirects(int){} void setUserAgent(const char *){}
 bool begin(WiFiClientSecure &,const char *url){lastURL=url;return true;}
 int GET(){return responseCode;}
 int writeToStream(HttpBodyBuffer *s){return s->write((const uint8_t *)response.data(),response.size());}
 void end(){}
};
struct Preferences {
 static std::map<std::string,std::vector<uint8_t>> records;
 bool begin(const char *name,bool){assert(!strcmp(name,"meshweather"));return true;}
 size_t getBytesLength(const char *key){return records[key].size();}
 size_t getBytes(const char *key,void *p,size_t n){if(n!=records[key].size())return 0;memcpy(p,records[key].data(),n);return n;}
 size_t putBytes(const char *key,const void *p,size_t n){if(failSave)return 0;auto *b=(const uint8_t *)p;records[key]={b,b+n};return n;}
 void end(){}
};
std::map<std::string,std::vector<uint8_t>> Preferences::records;
"""
tests = r"""
std::string readFile(const char *path){std::ifstream f(path);std::ostringstream s;s<<f.rdbuf();return s.str();}
void runJob(const std::string &body,int code=200){assert(queued);response=body;responseCode=code;auto fn=queued;queued=nullptr;fn(nullptr);meshweather::service();assert(owner==0);assert(!meshweather::busy());}
int main(int argc,char **argv){
 assert(argc==3);using namespace meshweather;
 const auto good=readFile(argv[1]),geo=readFile(argv[2]);
 Forecast f{};assert(parseForecast(good.data(),good.size(),defaultPlace,f));assert(f.count==5);
 const Forecast original=f;
 for(const std::string bad:{std::string("null"),good+"trailing",std::string(8193,'x'),std::string(17,'[')+std::string(17,']')}){
  assert(!parseForecast(bad.data(),bad.size(),defaultPlace,f));assert(!memcmp(&f,&original,sizeof(f)));
 }
 auto corrupt=[&](const char *object,const char *key,cJSON *replacement){
  auto *tree=cJSON_Parse(good.c_str());auto *part=cJSON_GetObjectItemCaseSensitive(tree,object);
  cJSON_ReplaceItemInObjectCaseSensitive(part,key,replacement);char *json=cJSON_PrintUnformatted(tree);
  assert(!parseForecast(json,strlen(json),defaultPlace,f));free(json);cJSON_Delete(tree);
 };
 corrupt("current","temperature_2m",cJSON_CreateNull());
 corrupt("current","relative_humidity_2m",cJSON_CreateNumber(101));
 corrupt("current","time",cJSON_CreateString("not-a-date-123456"));
 corrupt("current_units","temperature_2m",cJSON_CreateString("F"));
 corrupt("daily","time",cJSON_CreateArray());
 Forecast invalid=original;invalid.temperature=std::numeric_limits<float>::quiet_NaN();assert(!validForecast(invalid));
 invalid=original;invalid.days[0].low=invalid.days[0].high+1;assert(!validForecast(invalid));
 invalid=original;memset(invalid.place.name,'x',sizeof(invalid.place.name));assert(!validForecast(invalid));
 Place places[kPlaces]{};unsigned count=0;assert(parsePlaces(geo.data(),geo.size(),places,count)&&count==5);
 assert(!parsePlaces("[]",2,places,count));assert(parsePlaces("{}",2,places,count)&&count==0);
 const char *error="{\"error\":true}";assert(!parsePlaces(error,strlen(error),places,count));
 init();assert(!forecast());assert(!refresh());assert(!busy()&&owner==0);
 wifi=true;owner=2;assert(!refresh());assert(owner==2);owner=0;
 assert(refresh());assert(busy()&&owner==4);assert(!search("Zaragoza"));runJob(good);
 assert(forecast()&&forecast()->count==5&&loans==takes);
 const auto saved=Preferences::records["forecast"];const Forecast first=*forecast();
 loaded=false;haveCurrent=false;init();assert(forecast()&&forecast()->temperature==first.temperature);
 assert(!refresh(false));
 assert(refresh());runJob("unavailable",503);assert(!memcmp(forecast(),&first,sizeof(first)));assert(Preferences::records["forecast"]==saved);
 assert(refresh());runJob(good+"trailing");assert(Preferences::records["forecast"]==saved);
 testLocale="es";assert(!strcmp(description(0),"Despejado"));
 assert(search("San Severo, IT"));runJob(geo);assert(placeCount()==5);assert(lastURL.find("San%20Severo%2C%20IT")!=std::string::npos);
 assert(lastURL.find("language=es")!=std::string::npos);
 testLocale="it";assert(!strcmp(description(0),"Sereno"));
 const Place chosen=*place(1);assert(choose(1));runJob(good);assert(!strcmp(forecast()->place.name,chosen.name));
 char coords[80];snprintf(coords,sizeof(coords),"latitude=%.4f&longitude=%.4f",chosen.latitude,chosen.longitude);assert(lastURL.find(coords)!=std::string::npos);
 failTask=true;assert(!refresh());assert(owner==0&&!busy());failTask=false;
 failAllocation=true;assert(refresh());runJob(good);assert(loans==takes);failAllocation=false;
 failSave=true;assert(refresh());runJob(good);assert(strstr(status(),"salvataggio"));failSave=false;
 assert(refresh());runJob(std::string(8193,'x'));assert(loans==takes);
 Preferences::records["forecast"][0]^=1;loaded=false;haveCurrent=false;init();assert(!forecast());
 puts("PASS: weather units/scope, malformed/truncated/deep JSON, bounds, city selection, encoding, offline cache, busy network, failed HTTP/allocation/task/save, cache integrity and TLS/job release.");
}
"""
work = root / ".field-build/host-tests"
work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=work) as tmp:
    out = Path(tmp)
    (out / "Stream.h").write_text(
        "#pragma once\n#include <cstdint>\n#include <cstddef>\nclass Stream {public: virtual ~Stream()=default; "
        "virtual size_t write(uint8_t)=0;virtual size_t write(const uint8_t*,size_t)=0;"
        "virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;};\n"
    )
    flags = ["-g", "-fsanitize=address,undefined", "-I" + str(cjson)]
    subprocess.run(
        ["clang", *flags, "-c", str(cjson / "cJSON.c"), "-o", str(out / "cjson.o")],
        check=True,
    )
    (out / "test.cpp").write_text(stub + source + tests)
    subprocess.run(
        [
            "clang++",
            "-std=c++17",
            *flags,
            "-I" + str(out),
            "-I" + str(root / "src"),
            "-I" + str(root / "lib/meshtastic-device-ui/include"),
            str(out / "test.cpp"),
            str(out / "cjson.o"),
            "-o",
            str(out / "test"),
        ],
        check=True,
    )
    subprocess.run(
        [str(out / "test"), str(args.forecast.resolve()), str(args.places.resolve())],
        check=True,
    )
