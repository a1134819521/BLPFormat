execute_process(COMMAND "${CL}" /nologo /EP /DMSWindows=1
    /I${SDK}/photoshop /I${SDK}/pica_sp /I${SDK}/resources /Tc${SOURCE}
    OUTPUT_FILE "${OUTPUT}.rr" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CONVERTER}" "${OUTPUT}.rr" "${OUTPUT}"
    COMMAND_ERROR_IS_FATAL ANY)
