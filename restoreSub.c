#include <string.h>

#include <vxWorks.h>
#include <types.h>
#include <stdio.h>
#include <stdlib.h>
#include <callback.h>
#include <dbDefs.h>
#include <recSup.h>
#include <dbAccess.h>
#include <subRecord.h>
#include <iocsh.h>
#include <epicsExport.h>



extern int fdbrestore(char *filename);

struct rcallback {
   CALLBACK callback;
   struct subRecord *precord;
   char *pname;
};

static void restSubCallback(CALLBACK *pcallback) {
   
   struct rcallback *ourcb;

   callbackGetUser(ourcb, pcallback);

   ourcb->precord->c = fdbrestore(ourcb->pname);
   
   dbScanLock((struct dbCommon *)ourcb->precord);
   (ourcb->precord->rset->process)(ourcb->precord);
   dbScanUnlock((struct dbCommon *)ourcb->precord);

}

long devGDigRestInit();
long devGDigRestore();

static char restfilename[80] = "default.sav";

int devGDigSetRestFile(char *restfile) {
  strncpy(restfilename, restfile, 79);
  restfilename[79] = 0;
  return OK;
}

long devGDigRestInit(struct subRecord *psub) {
   
   struct rcallback *pcallback;
   
   pcallback = (struct rcallback *)calloc(1, sizeof(struct rcallback));
   psub->dpvt = (void *)pcallback;  
   callbackSetCallback(restSubCallback, &(pcallback->callback));
   callbackSetPriority(psub->prio, &(pcallback->callback));
   callbackSetUser(pcallback, &(pcallback->callback));
   pcallback->precord = psub;
   pcallback->pname = restfilename;
   return 0;
}

long devGDigRestore(struct subRecord *psub) {

    struct rcallback *pcallback;

    pcallback = (struct rcallback *)psub->dpvt;

    if (!psub->pact) {
        psub->pact = TRUE;
        callbackRequest(&(pcallback->callback));
     }
     return 0;
}

static const iocshArg devGDigRestInitArg0 = { "psub",iocshArgInt };
static const iocshArg * const devGDigRestInitArgs[1] = {
       &devGDigRestInitArg0};
static const iocshFuncDef devGDigRestInitFuncDef = {"devGDigRestInit",1,devGDigRestInitArgs};
static void devGDigRestInitCallFunc(const iocshArgBuf *args)
{
    devGDigRestInit((struct subRecord *)args[0].ival);
}
static const iocshArg devGDigRestoreArg0 = { "psub",iocshArgInt };
static const iocshArg * const devGDigRestoreArgs[1] = {
       &devGDigRestoreArg0};
static const iocshFuncDef devGDigRestoreFuncDef = {"devGDigRestore",1,devGDigRestoreArgs};
static void devGDigRestoreCallFunc(const iocshArgBuf *args)
{
    devGDigRestore((struct subRecord *)args[0].ival);
}

void devGDigRestoreRegistrar(void)
{
   iocshRegister(&devGDigRestInitFuncDef, devGDigRestInitCallFunc);
   iocshRegister(&devGDigRestoreFuncDef, devGDigRestoreCallFunc);
}
epicsExportRegistrar(devGDigRestoreRegistrar);
