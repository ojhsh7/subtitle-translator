#include "subtitle/text.h"
#include "subtitle/core.h"
#include <cstdio>
#include <string>
#include <chrono>
using namespace subtitle;
static void show(const char* id, const std::string& in, unsigned native){
  auto t0=std::chrono::steady_clock::now();
  auto segs=splitForTranslation(in,native);
  auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count();
  int tr=0; for(auto&s:segs) tr+=s.translate;
  printf("%s | segs=%zu translate=%d | %lldus |", id, segs.size(), tr, (long long)us);
  if(in.size()<200) for(auto&s:segs) printf(" [%s%s]", s.translate?"T:":"K:", s.utf8.c_str());
  // round trip with identity translations must reproduce the input
  std::vector<std::string> ids; for(auto&s:segs) if(s.translate) ids.push_back(s.utf8);
  printf(" | roundtrip=%s\n", joinTranslated(segs,ids)==in?"ok":"DIFF");
}
int main(){
  unsigned ko=preservableScripts("ko","");
  show("E01 empty","",ko);
  show("E02 spaces","   ",ko);
  show("E03 hangul only","안녕하세요 반가워요",ko);
  show("E04 mixed","채주아: Hello!",ko);
  show("E05 acronym","HP가 부족합니다. NPC에게 가세요",ko);
  show("E06 long acronym","HPMPXP 회복",ko);
  show("E07 emoji","😀 Hello 😀",ko);
  show("E08 emoji+ko","좋아요 😀",ko);
  show("E09 single misread","안녕 a 하세요",ko);
  show("E10 jp line","こんにちは、世界",ko);
  show("E11 ja target keeps kanji", "こんにちは、世界\n你好世界", preservableScripts("ja",""));
  show("E12 en target, en source", "Hello 世界", preservableScripts("en","en"));
  show("E13 zh only line target ko", "你好世界", ko);
  show("E14 ko 2-letter inside foreign", "I love 김치 so much", ko);
  show("E15 newline mix", "Hello\n안녕\nWorld", ko);
  std::string alt; for(int i=0;i<60;i++) alt += (i%2? "가나 ":"ab ");
  show("E16 60 alternations", alt, ko);
  std::string longEn; while(longEn.size()<6000) longEn+="The quick brown fox. ";
  show("E17 6000B english", longEn, ko);
  std::string invalid="abc\xff\xfe 한글 \xe3\x81"; show("E18 invalid utf8", invalid, ko);
  // core session: cache only compares with last source
  st_session* s=st_create();
  auto r=st_begin(s,1,10,10); printf("C01 accept empty=%d (ST_EMPTY expected)\n", st_accept_ocr(s,r,"   "));
  r=st_begin(s,1,10,10); printf("C02 accept=%d\n", st_accept_ocr(s,r,"Hello")); st_complete(s,r,"안녕");
  r=st_begin(s,1,10,10); printf("C03 same text cached=%d\n", st_accept_ocr(s,r,"Hello  "));
  r=st_begin(s,1,10,10); st_accept_ocr(s,r,"World"); st_complete(s,r,"세계");
  r=st_begin(s,1,10,10); printf("C04 A-B-A cached=%d (re-sends A)\n", st_accept_ocr(s,r,"Hello"));
  std::string big(40000,'a'); r=st_begin(s,1,10,10); printf("C05 too long=%d\n", st_accept_ocr(s,r,big.c_str()));
  printf("C06 begin while translating=%llu\n",(unsigned long long)st_begin(s,1,10,10));
  st_destroy(s);
}
