# Dump a small RAM window around the GSA200 globals (read-only).
init
halt
dump_image C:/Users/22191/Desktop/servo_app_v4.0/tools/ram_20000200.bin 0x20000200 256
echo "RAM_DUMP_OK"
resume
shutdown
