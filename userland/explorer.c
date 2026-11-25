#include "../include/axi.h"
#include "../include/vfs.h"
#include "../kernel/textile.h"
#include "../sdk/stdlib.h"
#include "../sdk/string.h"

#define A 0x70u
#define M 0x78u
#define S 0x1Fu
#define C 0x2Fu
#define D 0x4Fu
#define ESC 27
#define ENTER 13
#define BS 8
#define MAX_ITEMS VFS_MAX_NODES
#define TEXT_MAX 8192u
#define SYS_VFS_OPEN 47u
#define SYS_VFS_READ 48u
#define SYS_VFS_WRITE 49u
#define SYS_VFS_CLOSE 50u
#define SYS_VFS_MKDIR 51u
#define SYS_VFS_READDIR 52u
#define SYS_VFS_STAT 53u
#define SYS_VFS_UNLINK 56u

struct item { char name[VFS_NAME_MAX]; unsigned int size, type; };
struct state {
 unsigned int win, count, visible_count, visible[MAX_ITEMS], selected, first, view;
 struct item items[MAX_ITEMS];
 char path[VFS_PATH_MAX], search[17], input[VFS_NAME_MAX], target[VFS_PATH_MAX], note[56];
 char *buffer;
 unsigned int length, pos;
 int searching, naming, confirming, viewing, done;
};
static const char welcome[]="Welcome to FlairOS Explorer.\nCreated from the desktop workspace.\n";
void app_main(void);
static void entry(void){app_main();ax_call(SYS_EXIT,0,0,0);for(;;);}
__attribute__((section(".head"),used)) const unsigned int app_head[2]={TX_APP_MAGIC,(unsigned int)(unsigned long)entry};
static unsigned int call(unsigned int n,unsigned int b,unsigned int c,unsigned int d){return ax_call(n,b,c,d);}
static int vstat(const char*p,struct vfs_stat*s){return (int)call(SYS_VFS_STAT,(unsigned int)(unsigned long)p,(unsigned int)(unsigned long)s,0);}
static int readdir(const char*p,unsigned int i,struct vfs_dirent*e){return (int)call(SYS_VFS_READDIR,(unsigned int)(unsigned long)p,i,(unsigned int)(unsigned long)e);}
static int vopen(const char*p,unsigned int f){return (int)call(SYS_VFS_OPEN,(unsigned int)(unsigned long)p,f,0);}
static int vread(int fd,void*b,unsigned int n){return (int)call(SYS_VFS_READ,(unsigned int)fd,(unsigned int)(unsigned long)b,n);}
static int vwrite(int fd,const void*b,unsigned int n){return (int)call(SYS_VFS_WRITE,(unsigned int)fd,(unsigned int)(unsigned long)b,n);}
static void vclose(int fd){call(SYS_VFS_CLOSE,(unsigned int)fd,0,0);}
static int mkdirp(const char*p){return (int)call(SYS_VFS_MKDIR,(unsigned int)(unsigned long)p,0,0);}
static int unlinkp(const char*p){return (int)call(SYS_VFS_UNLINK,(unsigned int)(unsigned long)p,0,0);}
static unsigned int len(const char*s){unsigned int n=0;while(s&&s[n])n++;return n;}
static void copy(char*d,const char*s,unsigned int cap){unsigned int i=0;if(!cap)return;while(s&&s[i]&&i+1<cap){d[i]=s[i];i++;}d[i]=0;}
static void put(char*l,unsigned int x,const char*s,unsigned int max){unsigned int i=0;while(s&&s[i]&&i<max&&x+i<64){l[x+i]=s[i];i++;}}
static void blank(char*l){for(unsigned int i=0;i<64;i++)l[i]=' ';l[64]=0;}
static void num(char*out,unsigned int v){char t[11];unsigned int n=0;do{t[n++]=(char)('0'+v%10);v/=10;}while(v&&n<10);while(n)*out++=t[--n];*out=0;}
static void sizefmt(char*out,unsigned int v){char n[11];if(v<1024){num(n,v);copy(out,n,11);copy(out+len(out)," B",4);}else{unsigned int u=v<1048576?1024:1048576;num(n,v/u);copy(out,n,11);unsigned int p=len(out);out[p++]='.';out[p++]=(char)('0'+((v%u)*10)/u);out[p++]=' ';out[p++]=u==1024?'K':'M';out[p++]='B';out[p]=0;}}
static void status(struct state*s,const char*m){copy(s->note,m,sizeof(s->note));}
static int join(char*out,unsigned int cap,const char*base,const char*leaf){unsigned int p=len(base),n=len(leaf);if(!n||p+n+2>cap)return -1;copy(out,base,cap);if(base[p-1]!='/')out[p++]='/';for(unsigned int i=0;i<n;i++)out[p++]=leaf[i];out[p]=0;return 0;}
static void parent(struct state*s){unsigned int n=len(s->path);if(n<=1)return;while(n>1&&s->path[n-1]!='/')s->path[--n]=0;if(n>1)s->path[n-1]=0;if(!s->path[0])copy(s->path,"/",sizeof(s->path));}
static int contains(const char*t,const char*q){if(!q[0])return 1;for(unsigned int i=0;t[i];i++){unsigned int j=0;while(q[j]&&t[i+j]){char a=t[i+j],b=q[j];if(a>='a'&&a<='z')a-=32;if(b>='a'&&b<='z')b-=32;if(a!=b)break;j++;}if(!q[j])return 1;}return 0;}
static void filter(struct state*s){s->visible_count=0;for(unsigned int i=0;i<s->count;i++)if(contains(s->items[i].name,s->search))s->visible[s->visible_count++]=i;if(s->selected>=s->visible_count)s->selected=s->visible_count?s->visible_count-1:0;if(s->selected<s->first)s->first=s->selected;if(s->selected>=s->first+6)s->first=s->selected-5;}
static void refresh(struct state*s){struct vfs_dirent e;s->count=0;for(unsigned int i=0;i<MAX_ITEMS;i++){if(readdir(s->path,i,&e))break;struct item*t=&s->items[s->count++];copy(t->name,e.name,sizeof(t->name));t->size=e.size;t->type=e.type;}for(unsigned int i=1;i<s->count;i++){struct item t=s->items[i];unsigned int j=i;while(j&&((s->items[j-1].type!=VFS_TYPE_DIR&&t.type==VFS_TYPE_DIR)||(s->items[j-1].type==t.type&&strcmp(s->items[j-1].name,t.name)>0))){s->items[j]=s->items[j-1];j--;}s->items[j]=t;}s->first=s->selected=0;filter(s);}
static struct item*selected(struct state*s){return s->selected<s->visible_count?&s->items[s->visible[s->selected]]:0;}
static int selected_path(struct state*s,char*out){struct item*t=selected(s);return t?join(out,VFS_PATH_MAX,s->path,t->name):-1;}
static int is_apk_name(const char *s){unsigned int n=len(s);if(n<4)return 0;const char*e=s+n-4;return(e[0]=='.')&&(e[1]=='a'||e[1]=='A')&&(e[2]=='p'||e[2]=='P')&&(e[3]=='k'||e[3]=='K');}
static void open_item(struct state*s){struct item*t=selected(s);char p[VFS_PATH_MAX];if(!t||selected_path(s,p))return;if(t->type==VFS_TYPE_DIR){copy(s->path,p,sizeof(s->path));s->search[0]=0;refresh(s);status(s,"Folder opened. Backspace returns to parent.");return;}if(is_apk_name(t->name)){int res=ax_apk_launch(p);status(s,res>=0?"Launched Android APK in WinDown.":"Failed to launch APK.");return;}int fd=vopen(p,VFS_O_RDONLY);if(fd<0){status(s,"Could not open file.");return;}int n=vread(fd,s->buffer,TEXT_MAX);vclose(fd);if(n<0){status(s,"Read failed.");return;}s->length=(unsigned int)n;s->pos=0;s->viewing=1;}
static void create_note(struct state*s){char name[16],p[VFS_PATH_MAX];for(unsigned int i=1;i<=99;i++){name[0]='N';name[1]='O';name[2]='T';name[3]='E';name[4]=(char)('0'+i/10);name[5]=(char)('0'+i%10);name[6]='.';name[7]='T';name[8]='X';name[9]='T';name[10]=0;if(join(p,sizeof(p),s->path,name))break;struct vfs_stat st;if(!vstat(p,&st))continue;int fd=vopen(p,VFS_O_CREAT|VFS_O_WRONLY|VFS_O_TRUNC);if(fd<0){status(s,"Could not create NOTE##.TXT here.");return;}int n=vwrite(fd,welcome,sizeof(welcome)-1);vclose(fd);if(n!=(int)(sizeof(welcome)-1)){unlinkp(p);status(s,"Write failed; partial note removed.");return;}refresh(s);status(s,"New note created.");return;}status(s,"No available NOTE##.TXT name.");}
static void finish_folder(struct state*s){char p[VFS_PATH_MAX];if(!s->input[0]||!strcmp(s->input,".")||!strcmp(s->input,"..")||join(p,sizeof(p),s->path,s->input)){status(s,"Invalid folder name.");s->naming=0;return;}if(mkdirp(p))status(s,"Folder could not be created here.");else{refresh(s);status(s,"Folder created.");}s->naming=0;}
static void begin_delete(struct state*s){if(selected_path(s,s->target))return;s->confirming=1;status(s,"Delete item? Y confirms; N cancels.");}
static void finish_delete(struct state*s){if(unlinkp(s->target))status(s,"Delete failed: item may be read-only or folder not empty.");else status(s,"Item deleted.");s->confirming=0;s->target[0]=0;refresh(s);}
static void move_sel(struct state*s,int d){if(!s->visible_count)return;if(d<0&&s->selected)s->selected--;if(d>0&&s->selected+1<s->visible_count)s->selected++;if(s->selected<s->first)s->first=s->selected;if(s->selected>=s->first+6)s->first=s->selected-5;}
static void scroll(struct state*s,int d){if(d<0)s->pos=s->pos>48?s->pos-48:0;else if(s->pos+48<s->length)s->pos+=48;else s->pos=s->length;}
static void draw(struct state*s,unsigned int row,unsigned int attr,const char*l){ax_text(s->win,row,0,attr,l);}
static void ui(struct state*s){char l[65],n[12],sz[16];struct item*t=selected(s);blank(l);put(l,2,"EXPLORER",10);put(l,16,s->path,30);put(l,49,"ITEMS",6);num(n,s->visible_count);put(l,55,n,8);draw(s,0,C,l);
blank(l);put(l,2,"SEARCH",7);put(l,10,s->searching?(s->search[0]?s->search:"type..."):(s->search[0]?s->search:"press /"),18);put(l,35,"VIEW",5);put(l,42,s->view==1?"GRID":s->view==2?"DETAILS":"LIST",9);put(l,53,"BACKSPACE",10);draw(s,1,M,l);
if(s->view==1){for(unsigned int r=0;r<3;r++){blank(l);for(unsigned int c=0;c<2;c++){unsigned int ix=s->first+r*2+c;if(ix<s->visible_count){struct item*x=&s->items[s->visible[ix]];unsigned int col=c?34:2;put(l,col,ix==s->selected?">":" ",1);put(l,col+2,x->type==VFS_TYPE_DIR?"[DIR]":"[FILE]",6);put(l,col+9,x->name,22);}}draw(s,2+r,A,l);}}
else{blank(l);put(l,2,"NAME",20);if(s->view==2){put(l,38,"SIZE",8);put(l,52,"TYPE",8);}else put(l,52,"KIND",8);draw(s,2,M,l);for(unsigned int r=0;r<6;r++){unsigned int ix=s->first+r;blank(l);if(ix<s->visible_count){struct item*x=&s->items[s->visible[ix]];put(l,2,ix==s->selected?">":" ",1);put(l,4,x->name,28);if(s->view==2){sizefmt(sz,x->size);put(l,38,sz,12);}put(l,52,x->type==VFS_TYPE_DIR?"FOLDER":"FILE",8);}else if(!s->visible_count&&r==0)put(l,4,s->search[0]?"No matches.":"This folder is empty.",28);draw(s,3+r,ix==s->selected&&ix<s->visible_count?S:A,l);}}
blank(l);put(l,2,"SELECTED",10);if(t){put(l,13,t->name,22);if(t->type==VFS_TYPE_DIR)put(l,42,"FOLDER",8);else{sizefmt(sz,t->size);put(l,42,sz,12);}}else put(l,13,"No item selected",24);draw(s,9,M,l);
blank(l);put(l,2,s->naming?"NEW FOLDER:":s->confirming?"CONFIRM DELETE:":s->note,24);if(s->naming)put(l,16,s->input,30);if(s->confirming)put(l,21,s->target,38);draw(s,10,s->confirming?D:M,l);
blank(l);put(l,2,"N Note",9);put(l,12,"M Folder",12);put(l,27,"D Delete",11);put(l,41,"/ Search",11);put(l,54,"V View",9);draw(s,11,C,l);}
static void reader(struct state*s){char l[65],a[12],b[12];struct item*t=selected(s);blank(l);put(l,2,"PREVIEW",12);if(t)put(l,18,t->name,24);draw(s,0,C,l);blank(l);put(l,2,"TEXT CONTENT",16);put(l,47,"READ ONLY",12);draw(s,1,M,l);unsigned int p=s->pos;for(unsigned int r=0;r<8;r++){blank(l);unsigned int c=0;while(p<s->length&&c<60){char ch=s->buffer[p++];if(ch=='\n')break;if(ch=='\r')continue;if((unsigned char)ch<32)ch='.';l[2+c++]=ch;}draw(s,2+r,A,l);}blank(l);num(a,s->pos);num(b,s->length);put(l,2,"BYTE",5);put(l,8,a,10);put(l,24,"OF",3);put(l,29,b,10);draw(s,10,M,l);blank(l);put(l,2,"Esc Back",12);put(l,22,"Up/Down Scroll",18);draw(s,11,C,l);}
static void key(struct state*s,int k){if(s->confirming){if(k=='y'||k=='Y')finish_delete(s);else if(k=='n'||k=='N'||k==ESC){s->confirming=0;status(s,"Delete cancelled.");}return;}if(s->naming){if(k==ESC){s->naming=0;status(s,"Folder creation cancelled.");}else if(k==ENTER)finish_folder(s);else if(k==BS){unsigned int n=len(s->input);if(n)s->input[n-1]=0;}else if(k>=32&&k<127&&len(s->input)+1<sizeof(s->input)){unsigned int n=len(s->input);s->input[n]=(char)k;s->input[n+1]=0;}return;}if(s->searching){if(k==ESC){s->searching=0;s->search[0]=0;filter(s);}else if(k==ENTER){s->searching=0;status(s,"Search applied.");}else if(k==BS){unsigned int n=len(s->search);if(n)s->search[n-1]=0;filter(s);}else if(k>=32&&k<127&&len(s->search)<16){unsigned int n=len(s->search);s->search[n]=(char)k;s->search[n+1]=0;filter(s);}return;}if(s->viewing){if(k==ESC)s->viewing=0;else if(k==ARROW_UP||k=='w'||k=='W')scroll(s,-1);else if(k==ARROW_DOWN||k=='s'||k=='S')scroll(s,1);return;}if(k==ESC){s->done=1;return;}if(k==BS){parent(s);refresh(s);status(s,"Parent folder.");}else if(k==ARROW_UP||k=='w'||k=='W')move_sel(s,-1);else if(k==ARROW_DOWN||k=='s'||k=='S')move_sel(s,1);else if((k==ARROW_LEFT||k==ARROW_RIGHT)&&s->view==1)move_sel(s,k==ARROW_LEFT?-1:1);else if(k==ENTER)open_item(s);else if(k=='n'||k=='N')create_note(s);else if(k=='m'||k=='M'){s->naming=1;s->input[0]=0;status(s,"Type folder name, then press Enter.");}else if(k=='d'||k=='D')begin_delete(s);else if(k=='/') {s->searching=1;s->search[0]=0;}else if(k=='v'||k=='V'){s->view=(s->view+1)%3;s->first=s->view==1?(s->selected/6)*6:s->selected;status(s,"View changed.");}else if(k=='r'||k=='R'){refresh(s);status(s,"Folder refreshed.");}}
static void click(struct state*s,unsigned int r,unsigned int c){if(s->confirming){if(r==10)key(s,c<32?'n':'y');return;}if(s->naming||s->searching)return;if(s->viewing){if(r==11&&c<18)s->viewing=0;else if(r>=2&&r<10)scroll(s,1);return;}if(s->view==1&&r>=2&&r<5){unsigned int ix=s->first+(r-2)*2+(c>=32);if(ix<s->visible_count){if(ix==s->selected)open_item(s);else s->selected=ix;}}else if(s->view!=1&&r>=3&&r<9){unsigned int ix=s->first+r-3;if(ix<s->visible_count){if(ix==s->selected)open_item(s);else s->selected=ix;}}else if(r==11){if(c>=1&&c<10)create_note(s);else if(c>=12&&c<25){s->naming=1;s->input[0]=0;}else if(c>=27&&c<39)begin_delete(s);else if(c>=40&&c<52){s->searching=1;s->search[0]=0;}else if(c>=53)s->view=(s->view+1)%3;}}
void app_main(void){struct state*s=(struct state*)malloc(sizeof(*s));if(!s)return;memset(s,0,sizeof(*s));s->buffer=(char*)malloc(TEXT_MAX);if(!s->buffer){free(s);return;}copy(s->path,"/",sizeof(s->path));s->win=ax_open(7,2,64,13,"FlairOS Explorer");if(s->win==AX_FAIL){free(s->buffer);free(s);return;}status(s,"Ready. Enter opens; Backspace goes up; M creates a folder.");refresh(s);while(!s->done&&ax_alive(s->win)){if(s->viewing)reader(s);else ui(s);ax_present();int k=(int)ax_call(SYS_GETKEY,0,0,0);if(k>0)key(s,k);int a=(int)ax_call(SYS_GETARROW,0,0,0);if(a>=ARROW_UP&&a<=ARROW_RIGHT)key(s,a);unsigned int c=ax_click(s->win);if(c!=AX_FAIL)click(s,AX_CLICK_ROW(c),AX_CLICK_COL(c));if(ax_call(SYS_TOGGLE_PENDING,0,0,0))s->done=1;ax_call(SYS_SLEEP,1,0,0);}ax_close(s->win);free(s->buffer);free(s);}
