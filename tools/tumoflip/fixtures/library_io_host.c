static void source_write(Storage*s,const char*p,const char*text){char path[1400];local_path(s,p,path);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
static void source_equals(Storage*s,const char*p,const char*text){char path[1400],b[2048]={0};local_path(s,p,path);FILE*f=fopen(path,"rb");assert(f);size_t n=fread(b,1,sizeof(b)-1,f);assert(!ferror(f)&&n==strlen(text)&&!strcmp(b,text));fclose(f);}
static void blob_write(Storage*s,const char*p,const void*b,size_t n){char path[1400];local_path(s,p,path);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(b,1,n,f)==n);assert(!fclose(f));}
int main(int argc,char**argv){assert(argc==2);Storage s={0};strlcpy(s.root,argv[1],sizeof(s.root));
 assert(storage_simply_mkdir(&s,"/ext/apps_data"));assert(storage_simply_mkdir(&s,"/ext/subghz"));
 char(*empty_sources)[256]=calloc(32,256);uint32_t empty_count;bool empty_more;
 assert(history_sources(&s,0,empty_sources,&empty_count,&empty_more)&&empty_count==0);free(empty_sources);
 assert(!strcmp(file_history_ep()->appid,FILE_HISTORY_APP_ID));
 uint8_t digest[32];assert(!mbedtls_sha256((const uint8_t*)"abc",3,digest,0));assert(digest[0]==0xba&&digest[31]==0xad);
 const char*src="/ext/subghz/Sensor.sub";source_write(&s,src,"version one");
 assert(file_history_before_write(&s,src));
 assert(history_snapshot(&s,src));FileHistoryRecord r[4];assert(history_list(&s,src,r));
 source_write(&s,FILE_HISTORY_ENABLED,"");assert(!file_history_before_write(&s,src));source_equals(&s,src,"version one");
 assert(file_history_before_write(&s,"/ext/subghz/new.sub"));assert(file_history_before_write(&s,"/ext/subghz/file.txt"));assert(storage_common_remove(&s,FILE_HISTORY_ENABLED)==FSE_OK);
 uint32_t gen[4];for(unsigned i=0;i<4;i++)gen[i]=r[i].generation;int slot=library_newest_slot(gen);assert(slot>=0&&gen[slot]==1);
 assert(history_snapshot(&s,src));assert(history_list(&s,src,r));unsigned count=0;for(unsigned i=0;i<4;i++)if(r[i].generation)count++;assert(count==1);
 const int budgets[]={0,1,3,10,11,20};
 for(unsigned i=0;i<sizeof(budgets)/sizeof(*budgets);i++){source_write(&s,src,"version two");write_budget=budgets[i];assert(!history_snapshot(&s,src));write_budget=-1;source_equals(&s,src,"version two");assert(history_list(&s,src,r));assert(r[slot].generation==1);}
 sync_failure=true;assert(!history_snapshot(&s,src));sync_failure=false;source_equals(&s,src,"version two");
 assert(history_snapshot(&s,src));for(unsigned i=3;i<9;i++){char data[30];snprintf(data,sizeof(data),"version %u",i);source_write(&s,src,data);assert(history_snapshot(&s,src));}
 assert(history_list(&s,src,r));count=0;for(unsigned i=0;i<4;i++){gen[i]=r[i].generation;if(gen[i])count++;}assert(count==3);slot=library_newest_slot(gen);
 FuriString*dest=furi_string_alloc();assert(history_restore_copy(&s,src,slot,dest));source_equals(&s,src,"version 8");source_equals(&s,furi_string_get_cstr(dest),"version 8");
 assert(history_restore_copy(&s,src,slot,dest));assert(strstr(furi_string_get_cstr(dest),"restored_01"));
 assert(storage_common_remove(&s,src)==FSE_OK);char(*paths)[256]=calloc(32,256);uint32_t sources;bool more;assert(history_sources(&s,0,paths,&sources,&more)&&sources==1&&!more&&!strcmp(paths[0],src));
 assert(history_sources(&s,1,paths,&sources,&more)&&sources==0&&!more);
 assert(history_restore_copy(&s,src,slot,dest));assert(storage_common_stat(&s,src,NULL)==FSE_NOT_EXIST);
 FuriString*dir=furi_string_alloc();assert(history_dir(&s,src,dir,false));FuriString*bad=furi_string_alloc_printf("%s/%u.bin",furi_string_get_cstr(dir),slot);source_write(&s,furi_string_get_cstr(bad),"bad");assert(!history_restore_copy(&s,src,slot,dest));
 assert(!history_snapshot(&s,"/ext/subghz/../foo.sub"));assert(!history_snapshot(&s,"/ext/subghz/no.bin"));
 DeviceCard card={.name="Sensor",.notes="Original",.tags="home"},loaded={0};assert(library_card_save(&s,&card));
 strcpy(card.notes,"Changed");write_budget=15;assert(!library_card_save(&s,&card));write_budget=-1;assert(library_card_load(&s,"Sensor",&loaded)&&!strcmp(loaded.notes,"Original"));
 sync_failure=true;assert(!library_card_save(&s,&card));sync_failure=false;
 assert(library_card_save(&s,&card));assert(library_card_load(&s,"Sensor",&loaded)&&!strcmp(loaded.notes,"Changed"));
 strcpy(card.name,"../bad");assert(!library_card_save(&s,&card));
 assert(!library_card_load(&s,"../bad",&loaded));assert(!library_card_load(&s,"Missing",&loaded));strcpy(card.name,"Sensor");
 mkdir_failure=true;assert(!library_card_save(&s,&card));assert(!history_snapshot(&s,src));mkdir_failure=false;
 remove_failure=true;assert(!library_card_save(&s,&card));remove_failure=false;
 read_budget=1;assert(!library_card_load(&s,"Sensor",&loaded));read_budget=-1;
 sd_failure=true;assert(!library_card_load(&s,"Sensor",&loaded));assert(!history_list(&s,src,r));sd_failure=false;
 CardEnvelope ce;unsigned card_slot=0;uint32_t latest=0;for(unsigned i=0;i<4;i++){if(card_read(&s,"Sensor",i,&ce)&&ce.generation>latest){latest=ce.generation;card_slot=i;}}
 assert(card_read(&s,"Sensor",card_slot,&ce));CardEnvelope saved_card=ce;
 FuriString*cp=furi_string_alloc_printf(LIBRARY_CARDS "/Sensor/%u.card",card_slot);
 ce.magic=0;blob_write(&s,furi_string_get_cstr(cp),&ce,sizeof(ce));assert(!card_read(&s,"Sensor",card_slot,&ce));ce=saved_card;
 ce.generation=0;blob_write(&s,furi_string_get_cstr(cp),&ce,sizeof(ce));assert(!card_read(&s,"Sensor",card_slot,&ce));ce=saved_card;
 strcpy(ce.card.name,"Other");blob_write(&s,furi_string_get_cstr(cp),&ce,sizeof(ce));assert(!card_read(&s,"Sensor",card_slot,&ce));ce=saved_card;
 ce.checksum[0]^=1;blob_write(&s,furi_string_get_cstr(cp),&ce,sizeof(ce));assert(!card_read(&s,"Sensor",card_slot,&ce));ce=saved_card;
 ce.generation=UINT32_MAX;assert(!mbedtls_sha256((const uint8_t*)&ce,offsetof(CardEnvelope,checksum),ce.checksum,0));blob_write(&s,furi_string_get_cstr(cp),&ce,sizeof(ce));assert(!library_card_save(&s,&card));
 const char*roll="/ext/subghz/Rollover.sub";source_write(&s,roll,"old");assert(history_snapshot(&s,roll));
 FuriString*rd=furi_string_alloc();assert(history_dir(&s,roll,rd,false));HistoryEnvelope he;assert(history_read_record(&s,furi_string_get_cstr(rd),0,&he));HistoryEnvelope original=he;
 FuriString*hp=furi_string_alloc_printf("%s/0.meta",furi_string_get_cstr(rd));
 he.magic=0;blob_write(&s,furi_string_get_cstr(hp),&he,sizeof(he));assert(!history_read_record(&s,furi_string_get_cstr(rd),0,&he));he=original;
 he.record.generation=0;blob_write(&s,furi_string_get_cstr(hp),&he,sizeof(he));assert(!history_read_record(&s,furi_string_get_cstr(rd),0,&he));he=original;
 he.record.size=FILE_HISTORY_MAX_BYTES+1;blob_write(&s,furi_string_get_cstr(hp),&he,sizeof(he));assert(!history_read_record(&s,furi_string_get_cstr(rd),0,&he));he=original;
 memset(he.record.source,'x',sizeof(he.record.source));blob_write(&s,furi_string_get_cstr(hp),&he,sizeof(he));assert(!history_read_record(&s,furi_string_get_cstr(rd),0,&he));he=original;
 he.checksum[0]^=1;blob_write(&s,furi_string_get_cstr(hp),&he,sizeof(he));assert(!history_read_record(&s,furi_string_get_cstr(rd),0,&he));he=original;
 he.record.generation=UINT32_MAX;assert(!mbedtls_sha256((const uint8_t*)&he.record,sizeof(he.record),he.checksum,0));blob_write(&s,furi_string_get_cstr(hp),&he,sizeof(he));source_write(&s,roll,"new");assert(!history_snapshot(&s,roll));source_equals(&s,roll,"new");
 assert(!history_restore_copy(&s,roll,4,dest));assert(!history_restore_copy(&s,"/ext/bad.bin",0,dest));assert(!history_restore_copy(&s,"/ext/missing.sub",0,dest));
 assert(!history_copy_new(&s,roll,roll));source_equals(&s,roll,"new");assert(!history_copy_new(&s,"/ext/missing.sub",roll));
 uint32_t hash_size;read_budget=0;assert(!history_hash_file(&s,roll,digest,&hash_size));read_budget=-1;
 char huge[1400];local_path(&s,"/ext/subghz/Huge.sub",huge);FILE*hf=fopen(huge,"wb");assert(hf);assert(!ftruncate(fileno(hf),FILE_HISTORY_MAX_BYTES+1));fclose(hf);assert(!history_snapshot(&s,"/ext/subghz/Huge.sub"));
 char longsrc[256];strcpy(longsrc,"/ext/subghz/");memset(longsrc+strlen(longsrc),'A',230);strcpy(longsrc+strlen("/ext/subghz/")+230,".sub");source_write(&s,longsrc,"long");assert(history_snapshot(&s,longsrc));assert(!history_restore_copy(&s,longsrc,0,dest));
 he=original;strcpy(he.record.source,"/ext/subghz/Else.sub");assert(!mbedtls_sha256((const uint8_t*)&he.record,sizeof(he.record),he.checksum,0));blob_write(&s,furi_string_get_cstr(hp),&he,sizeof(he));
 assert(!history_list(&s,roll,r));assert(!history_restore_copy(&s,roll,0,dest));assert(history_sources(&s,0,paths,&sources,&more));
 for(unsigned i=0;i<sources;i++)assert(strcmp(paths[i],"/ext/subghz/Else.sub"));
 blob_write(&s,furi_string_get_cstr(hp),&original,sizeof(original));
 remove_failure=true;assert(!history_snapshot(&s,roll));remove_failure=false;
 read_budget=0;assert(!history_copy_new(&s,roll,"/ext/subghz/failed.sub"));read_budget=-1;assert(storage_common_stat(&s,"/ext/subghz/failed.sub",NULL)==FSE_NOT_EXIST);
 for(unsigned i=0;i<33;i++){char source[64];snprintf(source,sizeof(source),"/ext/subghz/Page-%02u.sub",i);source_write(&s,source,"page");assert(history_snapshot(&s,source));}
 assert(history_sources(&s,0,paths,&sources,&more)&&sources==32&&more);assert(history_sources(&s,32,paths,&sources,&more)&&sources==4&&!more);
 furi_string_free(cp);furi_string_free(rd);furi_string_free(hp);
 free(paths);furi_string_free(dest);furi_string_free(dir);furi_string_free(bad);return 0;}
