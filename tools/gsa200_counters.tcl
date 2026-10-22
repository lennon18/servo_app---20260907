# Read GSA200 runtime counters (read-only).
init
halt
echo "--- GSA200 counters ---"
set v [lindex [read_memory 0x20000270 32 1] 0]
echo "active_baudrate         = $v"
set v [lindex [read_memory 0x20000274 32 1] 0]
echo "uart_errors_per_sec     = $v"
set v [lindex [read_memory 0x20000278 32 1] 0]
echo "dropped_frames_per_sec  = $v"
set v [lindex [read_memory 0x2000027c 32 1] 0]
echo "sequence_errors_per_sec = $v"
set v [lindex [read_memory 0x20000280 32 1] 0]
echo "format_errors_per_sec   = $v"
set v [lindex [read_memory 0x20000284 32 1] 0]
echo "checksum_errors_per_sec = $v"
set v [lindex [read_memory 0x20000288 32 1] 0]
echo "valid_frames_per_sec    = $v"
set v [lindex [read_memory 0x2000028c 32 1] 0]
echo "uart_errors             = $v"
set v [lindex [read_memory 0x20000290 32 1] 0]
echo "dropped_frames          = $v"
set v [lindex [read_memory 0x20000294 32 1] 0]
echo "sequence_errors         = $v"
set v [lindex [read_memory 0x20000298 32 1] 0]
echo "format_errors           = $v"
set v [lindex [read_memory 0x2000029c 32 1] 0]
echo "checksum_errors         = $v"
set v [lindex [read_memory 0x200002a0 32 1] 0]
echo "valid_frames            = $v"
set v [lindex [read_memory 0x200002a4 32 1] 0]
echo "sample_counter          = $v"
set v [lindex [read_memory 0x20000268 8 1] 0]
echo "data_valid              = $v"
resume
shutdown
