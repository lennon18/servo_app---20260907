# GSA200 raw-frame capture (read-only).
# Halts the core, reads the UART2 circular-DMA write pointer (DMA1_Stream3->NDTR),
# then dumps the 512 bytes ending at that pointer (handles ring wrap).
# No memory or peripheral is written. The core is resumed before shutdown.

init
halt

set ndtr [lindex [read_memory 0x4002005C 32 1] 0]
echo "NDTR=$ndtr"
set w [expr {4096 - $ndtr}]
echo "WRITE_INDEX=$w"

if {$w >= 512} {
    dump_image C:/Users/22191/Desktop/servo_app_v4.0/tools/ring_a.bin [expr {0x30020000 + $w - 512}] 512
    echo "DUMP_MODE=contiguous"
    echo "DUMP_A_OK"
} else {
    dump_image C:/Users/22191/Desktop/servo_app_v4.0/tools/ring_a.bin [expr {0x30020F00 + $w}] [expr {512 - $w}]
    dump_image C:/Users/22191/Desktop/servo_app_v4.0/tools/ring_b.bin 0x30020000 $w
    echo "DUMP_MODE=wrapped"
    echo "DUMP_A_OK"
    echo "DUMP_B_OK"
}

# Also read the parser's 32-byte frame buffer and index for cross-checking.
# frame is at 0x20000248, frame_index at 0x20000241 (from the ELF symbols).
dump_image C:/Users/22191/Desktop/servo_app_v4.0/tools/parser_frame.bin 0x20000248 32
echo "PARSER_FRAME_OK"

resume
shutdown
