"""Portable objects are checked copies, never destination-path instructions."""
import unittest
from tools.tumoflip.test_field_wallet import storage_native

SOURCES = ("field_io.h", "field_io.c", "field_bundle.h", "field_bundle.c")


class FieldBundleTests(unittest.TestCase):
    def test_valid_package_replacement_between_validation_and_copy_is_rejected(self):
        storage_native(r'''
static bool replaced;
static bool replace(void*c,const char*p,uint32_t n){(void)n;if(replaced||!p||!strstr(p,".sub"))return true;replaced=true;Storage*s=c;
 char host[1400];local_path(s,"/ext/Object.tobj",host);FILE*f=fopen(host,"r+b");assert(f);FieldBundleHeader h;assert(fread(&h,1,sizeof(h),f)==sizeof(h));strcpy(h.card.notes,"Different validated object");assert(!mbedtls_sha256((const uint8_t*)&h,offsetof(FieldBundleHeader,hash),h.hash,0));rewind(f);assert(fwrite(&h,1,sizeof(h),f)==sizeof(h));assert(!fclose(f));return true;}
int main(int argc,char**argv){assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);assert(storage_simply_mkdir(&s,"/ext/apps_data"));assert(storage_simply_mkdir(&s,"/ext/subghz"));
 File*f=storage_file_alloc(&s);assert(storage_file_open(f,"/ext/subghz/Own.sub",FSAM_WRITE,FSOM_CREATE_NEW));assert(storage_file_write(f,"Own",3)==3);assert(storage_file_close(f));storage_file_free(f);
 DeviceCard card={.name="Office",.link_count=1,.links={"/ext/subghz/Own.sub"}};
 assert(field_bundle_export(&s,&card,"/ext/Object.tobj",NULL,NULL));
 assert(!field_bundle_import(&s,"/ext/Object.tobj","Changed",replace,&s));assert(!library_card_load(&s,"Changed",&card));return 0;}
''', SOURCES)

    def test_verified_export_import_collisions_corruption_and_cancellation(self):
        storage_native(r'''
static bool cancel(void*c,const char*p,uint32_t n){(void)c;(void)p;(void)n;return false;}
static void put(Storage*s,const char*p,const char*text){File*f=storage_file_alloc(s);assert(storage_file_open(f,p,FSAM_WRITE,FSOM_CREATE_NEW));assert(storage_file_write(f,text,strlen(text))==strlen(text));assert(storage_file_close(f));storage_file_free(f);}
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);
 assert(storage_simply_mkdir(&s,"/ext/apps_data"));assert(storage_simply_mkdir(&s,"/ext/subghz"));
 DeviceCard card={.name="Office",.notes="My own receiver",.link_count=1,.links={"/ext/subghz/Own.sub"},.labels={"Front gate"}},read;
 const char*original="Filetype: Flipper SubGhz Key File\nProtocol: RAW\nRAW_Data: 100 -100\n";
 put(&s,card.links[0],original);
 const char*pack="/ext/Object.tobj";
 assert(field_bundle_export(&s,&card,pack,NULL,NULL));
 assert(!field_bundle_export(&s,&card,pack,NULL,NULL));
 assert(field_bundle_inspect(&s,pack,&read,NULL,NULL)&&!memcmp(&card,&read,sizeof(card)));
 uint8_t receipt[32],wrong[32];assert(field_bundle_inspect_receipt(&s,pack,&read,receipt,NULL,NULL));memcpy(wrong,receipt,32);wrong[0]^=1;
 assert(!field_bundle_import_receipt(&s,pack,"Wrong",wrong,NULL,NULL));assert(!library_card_load(&s,"Wrong",&read));
 assert(field_bundle_import_receipt(&s,pack,"Imported",receipt,NULL,NULL));
 assert(library_card_load(&s,"Imported",&read)&&read.link_count==1&&!strcmp(read.labels[0],"Front gate"));
 assert(strcmp(read.links[0],card.links[0])&&strstr(read.links[0],"/imports/Imported/"));
 uint32_t n1,n2;uint8_t h1[32],h2[32];assert(field_file_hash(&s,read.links[0],&n1,h1,NULL,NULL));assert(field_file_hash(&s,card.links[0],&n2,h2,NULL,NULL));assert(n1==n2&&!memcmp(h1,h2,32));
 assert(!field_bundle_import(&s,pack,"Imported",NULL,NULL));
 assert(!field_bundle_import(&s,pack,"../Overwrite",NULL,NULL));
 assert(!field_bundle_import(&s,pack,"Cancelled",cancel,NULL));
 assert(!field_bundle_inspect(&s,pack,&read,cancel,NULL));
 write_budget=1;assert(!field_bundle_export(&s,&card,"/ext/Partial.tobj",NULL,NULL));write_budget=-1;
 assert(storage_common_stat(&s,"/ext/Partial.tobj",NULL)==FSE_NOT_EXIST);
 sync_failure=true;assert(!field_bundle_export(&s,&card,"/ext/Sync.tobj",NULL,NULL));sync_failure=false;
 assert(!field_bundle_export(&s,&card,"/ext/Cancel.tobj",cancel,NULL));
 write_budget=1;assert(!field_bundle_import(&s,pack,"Failed",NULL,NULL));write_budget=-1;assert(!library_card_load(&s,"Failed",&read));
 char host[1400];local_path(&s,pack,host);FILE*f=fopen(host,"r+b");assert(f);assert(!fseek(f,-1,SEEK_END));int byte=fgetc(f);assert(byte>=0);assert(!fseek(f,-1,SEEK_END));fputc(byte^1,f);assert(!fclose(f));
 assert(!field_bundle_inspect(&s,pack,&read,NULL,NULL));assert(!field_bundle_import(&s,pack,"Corrupt",NULL,NULL));
 assert(!library_card_load(&s,"Corrupt",&read));
 assert(field_file_hash(&s,card.links[0],&n1,h1,NULL,NULL)&&n1==n2&&!memcmp(h1,h2,32));
 return 0;
}
''', SOURCES)


if __name__ == "__main__":
    unittest.main()
