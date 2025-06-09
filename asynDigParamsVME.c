createParam("vme_gp_ctrl",asynParamUInt32Digital,&vme_gp_ctrl);
createParam("vme_clk_ctrl",asynParamUInt32Digital,&vme_clk_ctrl);
createParam("VME_MON_STATUS",asynParamUInt32Digital,&VME_MON_STATUS);
createParam("SERIAL_NUMBER",asynParamUInt32Digital,&SERIAL_NUMBER);

setUIntDigitalParam(vme_gp_ctrl,1,0xffffffff);
setUIntDigitalParam(vme_clk_ctrl,1,0xffffffff);
setUIntDigitalParam(VME_MON_STATUS,1,0xffffffff);
setUIntDigitalParam(SERIAL_NUMBER,1,0xffffffff);

setAddress(vme_gp_ctrl,0x0900);
setAddress(vme_clk_ctrl,0x0910);
setAddress(VME_MON_STATUS,0x0908);
setAddress(SERIAL_NUMBER,0x0920);

