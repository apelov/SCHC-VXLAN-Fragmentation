#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int rejected(const char *source,const char *needle,const char *replacement,const char *expected) {
 FILE *in=fopen(source,"r"),*out; char line[1024],e[256]; struct schcvx_config c; int found=0,rc; if(!in)return 1; out=fopen("test-config-invalid.tmp","w"); if(!out) { fclose(in); return 1; }
 while(fgets(line,sizeof line,in)) { if(!strncmp(line,needle,strlen(needle))) { if(replacement) fputs(replacement,out); found=1; } else fputs(line,out); } fclose(in); fclose(out); rc=schcvx_config_load("test-config-invalid.tmp",&c,e,sizeof e); remove("test-config-invalid.tmp"); if(!found || rc==0 || !strstr(e,expected)) { fprintf(stderr,"expected rejection naming %s, got %s\n",expected,rc?e:"success"); return 1; } return 0;
}
int main(int argc,char **argv) { struct schcvx_config c; char e[256]; if(argc!=2 || schcvx_config_load(argv[1],&c,e,sizeof e)) { fprintf(stderr,"config: %s\n",argc==2?e:"missing path"); return 1; } if(c.count!=1 || c.assoc[0].tx.max_frame_len!=9014 || strcmp(c.assoc[0].name,"lab")) return 1;
 return rejected(argv[1],"S =",NULL,"S") || rejected(argv[1],"R =","R = 1\n","R") || rejected(argv[1],"Q =","Q = 1\n","Q") || rejected(argv[1],"fr_rule_id =","fr_rule_id = 0\n","fr_rule_id") || rejected(argv[1],"tap =","unexpected = x\n","unexpected") || rejected(argv[1],"pmtu =","pmtu = 100\n","pmtu"); }
