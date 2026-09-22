static void source_write(Storage*s,const char*p,const char*text){char path[1400];local_path(s,p,path);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
static void source_equals(Storage*s,const char*p,const char*text){char path[1400],b[2048]={0};local_path(s,p,path);FILE*f=fopen(path,"rb");assert(f);size_t n=fread(b,1,sizeof(b)-1,f);assert(!ferror(f)&&n==strlen(text)&&!strcmp(b,text));fclose(f);}
int main(int argc,char**argv){assert(argc==2);Storage s={0};strlcpy(s.root,argv[1],sizeof(s.root));
 assert(storage_simply_mkdir(&s,"/ext/apps_data"));assert(storage_simply_mkdir(&s,"/ext/subghz"));
 uint8_t digest[32];assert(!mbedtls_sha256((const uint8_t*)"abc",3,digest,0));assert(digest[0]==0xba&&digest[31]==0xad);
 const char*src="/ext/subghz/Sensor.sub";source_write(&s,src,"version one");
 assert(history_snapshot(&s,src));FileHistoryRecord r[4];assert(history_list(&s,src,r));
 uint32_t gen[4];for(unsigned i=0;i<4;i++)gen[i]=r[i].generation;int slot=library_newest_slot(gen);assert(slot>=0&&gen[slot]==1);
 assert(history_snapshot(&s,src));assert(history_list(&s,src,r));unsigned count=0;for(unsigned i=0;i<4;i++)if(r[i].generation)count++;assert(count==1);
 const int budgets[]={0,1,3,10,11,20};
 for(unsigned i=0;i<sizeof(budgets)/sizeof(*budgets);i++){source_write(&s,src,"version two");write_budget=budgets[i];assert(!history_snapshot(&s,src));write_budget=-1;source_equals(&s,src,"version two");assert(history_list(&s,src,r));assert(r[slot].generation==1);}
 sync_failure=true;assert(!history_snapshot(&s,src));sync_failure=false;source_equals(&s,src,"version two");
 assert(history_snapshot(&s,src));for(unsigned i=3;i<9;i++){char data[30];snprintf(data,sizeof(data),"version %u",i);source_write(&s,src,data);assert(history_snapshot(&s,src));}
 assert(history_list(&s,src,r));count=0;for(unsigned i=0;i<4;i++){gen[i]=r[i].generation;if(gen[i])count++;}assert(count==3);slot=library_newest_slot(gen);
 FuriString*dest=furi_string_alloc();assert(history_restore_copy(&s,src,slot,dest));source_equals(&s,src,"version 8");source_equals(&s,furi_string_get_cstr(dest),"version 8");
 assert(history_restore_copy(&s,src,slot,dest));assert(strstr(furi_string_get_cstr(dest),"restored_01"));
 assert(storage_common_remove(&s,src)==FSE_OK);char(*paths)[256]=calloc(32,256);uint32_t sources;assert(history_sources(&s,paths,&sources)&&sources==1&&!strcmp(paths[0],src));
 assert(history_restore_copy(&s,src,slot,dest));assert(storage_common_stat(&s,src,NULL)==FSE_NOT_EXIST);
 FuriString*dir=furi_string_alloc();assert(history_dir(&s,src,dir,false));FuriString*bad=furi_string_alloc_printf("%s/%u.bin",furi_string_get_cstr(dir),slot);source_write(&s,furi_string_get_cstr(bad),"bad");assert(!history_restore_copy(&s,src,slot,dest));
 assert(!history_snapshot(&s,"/ext/subghz/../foo.sub"));assert(!history_snapshot(&s,"/ext/subghz/no.bin"));
 DeviceCard card={.name="Sensor",.notes="Original",.tags="home"},loaded={0};assert(library_card_save(&s,&card));
 strcpy(card.notes,"Changed");write_budget=15;assert(!library_card_save(&s,&card));write_budget=-1;assert(library_card_load(&s,"Sensor",&loaded)&&!strcmp(loaded.notes,"Original"));
 sync_failure=true;assert(!library_card_save(&s,&card));sync_failure=false;
 assert(library_card_save(&s,&card));assert(library_card_load(&s,"Sensor",&loaded)&&!strcmp(loaded.notes,"Changed"));
 strcpy(card.name,"../bad");assert(!library_card_save(&s,&card));
 free(paths);furi_string_free(dest);furi_string_free(dir);furi_string_free(bad);return 0;}
