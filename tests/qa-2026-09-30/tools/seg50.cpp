#include "subtitle/text.h"
#include <cstdio>
int main(){ using namespace subtitle; std::string s; for(int i=0;i<60;i++) s+="abc 가나다 ";
 auto v=splitForTranslation(s,preservableScripts("ko","")); int t=0; for(auto&x:v) t+=x.translate; printf("bytes=%zu translate_segments=%d (MaxTexts=50)\n",s.size(),t);}
