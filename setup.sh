SETUP_CONFIG=setup_config.sh

[ -f $SETUP_CONFIG ] && source $SETUP_CONFIG

echo "Setup Nuclei SDK Tool Environment"
echo "NUCLEI_TOOL_ROOT=$NUCLEI_TOOL_ROOT"

export PATH=$NUCLEI_TOOL_ROOT/gcc/bin:$NUCLEI_TOOL_ROOT/openocd/bin:$NUCLEI_TOOL_ROOT/qemu/bin:$NUCLEI_TOOL_ROOT/nucleimodel/bin:$PATH
