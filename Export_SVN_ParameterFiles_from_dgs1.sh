## script to pull down all spreadsheet-generated driver source files from SVN.
##
##	this script is supposed to be used from a dgs1 login to /dk/fs2/dgs/global_sandbox/devel/dgsDrivers/dgsDriverApp/src

cd /dk/fs2/dgs/global_sandbox/devel/dgsDrivers/dgsDriverApp/src


###########################################
## Collect user name and password to use for SVN.
###########################################
read -p "Enter SVN Username:" SVN_USERNAME
read -s -p "Enter SVN Password(will not echo):" SVN_PASS

###########################################
##  remove existing "parameter" files
##
##	Use non-Torbened versions of rm and cp everywhere by direct access to /usr/bin
###########################################
echo "removing all current parameter files for MTRG, RTRG, Digitizer"
/bin/rm -f ./*Param*.c
/bin/rm -f ./*Param*.h

echo "Remove done"
echo ""
echo ""

###########################################
##  export template files from SVN.
##  this requires passing commands from this batch file to dgs1 as con6 does not have svn.
###########################################
echo "About to export 8 files (2 MTRG, 2 RTRG, 4 Digitizer)"

svn --username ${SVN_USERNAME} --password ${SVN_PASS} export https://svn.inside.anl.gov/repos/psg/CodeGeneratingSpreadsheetGeneric/Projects/DGS_Mtrig/SS_output/asynMTrigParams.c ./asynMTrigParams.c
svn --username ${SVN_USERNAME} --password ${SVN_PASS} export https://svn.inside.anl.gov/repos/psg/CodeGeneratingSpreadsheetGeneric/Projects/DGS_Mtrig/SS_output/asynMTrigParams.h ./asynMTrigParams.h
svn --username ${SVN_USERNAME} --password ${SVN_PASS} export https://svn.inside.anl.gov/repos/psg/CodeGeneratingSpreadsheetGeneric/Projects/DGS_Rtrig/SS_output/asynRTrigParams.c ./asynRTrigParams.c
svn --username ${SVN_USERNAME} --password ${SVN_PASS} export https://svn.inside.anl.gov/repos/psg/CodeGeneratingSpreadsheetGeneric/Projects/DGS_Rtrig/SS_output/asynRTrigParams.h ./asynRTrigParams.h
svn --username ${SVN_USERNAME} --password ${SVN_PASS} export https://svn.inside.anl.gov/repos/psg/CodeGeneratingSpreadsheetGeneric/Projects/DGS_CSdigitizer/SS_output/asynDigParams.c ./asynDigParams.c
svn --username ${SVN_USERNAME} --password ${SVN_PASS} export https://svn.inside.anl.gov/repos/psg/CodeGeneratingSpreadsheetGeneric/Projects/DGS_CSdigitizer/SS_output/asynDigParams.h ./asynDigParams.h
##add the digitizer VME separate param files 20240221  jta
svn --username ${SVN_USERNAME} --password ${SVN_PASS} export https://svn.inside.anl.gov/repos/psg/CodeGeneratingSpreadsheetGeneric/Projects/DGS_CSdigitizerVME/SS_output/asynDigParamsVME.c ./asynDigParamsVME.c
svn --username ${SVN_USERNAME} --password ${SVN_PASS} export https://svn.inside.anl.gov/repos/psg/CodeGeneratingSpreadsheetGeneric/Projects/DGS_CSdigitizerVME/SS_output/asynDigParamsVME.h ./asynDigParamsVME.h


## for the #include in asynDigitizerDriver to work, the files asynDigParams.c and asynDigParamsVME.c apparently need to be merged into the file
## MergedAsynDigParams.c, and the process repeated for the .h files.
cat asynDigParams.c > MergedAsynDigParams.c
cat asynDigParamsVME.c >> MergedAsynDigParams.c
cat asynDigParams.h > MergedAsynDigParams.h
cat asynDigParamsVME.h >> MergedAsynDigParams.h
