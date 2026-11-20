#!/bin/sh
set -eu

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d)
export MOTOR_TEST_ROOT="$test_dir/board"
export SAMPLE_MOTOR_STATE_DIR="$test_dir/runtime with spaces"
export MOTOR_TEST_BUSYBOX="$(command -v busybox)"
export MOTOR_TEST_BIN="$test_dir/bin"
mkdir -p "$test_dir/bin" "$test_dir/with spaces" "$MOTOR_TEST_ROOT/registers"
cp "$source_dir/sample_motor" "$test_dir/with spaces/sample_motor"
sample="$test_dir/with spaces/sample_motor"

cleanup() {
    test_result=$?
    if [ "$test_result" -ne 0 ] && [ -f "$SAMPLE_MOTOR_STATE_DIR/motor.log" ]; then
        cat "$SAMPLE_MOTOR_STATE_DIR/motor.log" >&2
    fi
    if [ -d "$SAMPLE_MOTOR_STATE_DIR" ]; then
        echo stop >"$SAMPLE_MOTOR_STATE_DIR/stop"
    fi
    cleanup_wait=0
    while ! (exec 9>/tmp/pt100-motor.lock; flock -n 9); do
        cleanup_wait=$((cleanup_wait + 1))
        if [ "$cleanup_wait" -gt 100 ]; then
            echo "Test worker still holds lock; preserving $test_dir" >&2
            return
        fi
        "$MOTOR_TEST_BUSYBOX" usleep 50000
    done
    if [ "${MOTOR_TEST_KEEP:-0}" = 1 ]; then
        echo "Test files: $test_dir" >&2
    else
        rm -rf "$test_dir"
    fi
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM

cat >"$test_dir/bin/board-tool" <<'EOF'
#!/bin/sh
set -eu
tool=${0##*/}
case "$tool" in
    id) echo "${MOTOR_TEST_UID:-0}" ;;
    usleep) exec "$MOTOR_TEST_BUSYBOX" usleep "$@" ;;
    devmem)
        printf 'MEM %s\n' "$*" >>"$MOTOR_TEST_ROOT/trace"
        case "$1" in
            0x03001088) file=mux; fallback=3 ;;
            0x05021004) file=direction; fallback=16 ;;
            0x05021000) file=gpio; fallback=64 ;;
            *) exit 20 ;;
        esac
        if [ "$#" -eq 3 ]; then
            [ "$1" = 0x05021000 ] || exit 21
            echo "$(($3))" >"$MOTOR_TEST_ROOT/$file"
        elif [ -f "$MOTOR_TEST_ROOT/$file" ]; then
            printf '0x%08x\n' "$(cat "$MOTOR_TEST_ROOT/$file")"
        else
            printf '0x%08x\n' "$fallback"
        fi
        ;;
    i2ctransfer)
        [ "$1 $2" = '-y 0' ] || exit 22
        transfer=$3 address=$(($4))
        case "$transfer" in
            w1@0x56)
                [ "$#" -eq 5 ] && [ "$5" = r1 ] || exit 23
                printf 'R %s\n' "$address" >>"$MOTOR_TEST_ROOT/trace"
                [ ! -f "$MOTOR_TEST_ROOT/i2c_fail" ] || exit 8
                if [ -f "$MOTOR_TEST_ROOT/bad_response" ]; then echo invalid; exit 0; fi
                if [ "$address" -eq 43 ] && [ -f "$MOTOR_TEST_ROOT/refresh_fail" ]; then echo 0x02; exit 0; fi
                if [ -f "$MOTOR_TEST_ROOT/registers/$address" ]; then
                    value=$(cat "$MOTOR_TEST_ROOT/registers/$address")
                else
                    case "$address" in
                        0) value=129 ;; 1) value=108 ;; 2|37|69) value=128 ;;
                        4) value=144 ;; 32|64) value=23 ;; 33|65) value=49 ;;
                        *) value=0 ;;
                    esac
                fi
                printf '0x%02x\n' "$value"
                ;;
            w2@0x56|w3@0x56)
                value=$(($5))
                [ "$transfer" != w3@0x56 ] || value=$((value | ($6 << 8)))
                printf 'W %s %s\n' "$address" "$value" >>"$MOTOR_TEST_ROOT/trace"
                [ ! -f "$MOTOR_TEST_ROOT/i2c_fail" ] || exit 8
                if [ -f "$MOTOR_TEST_ROOT/fail_write" ] &&
                    [ "$(cat "$MOTOR_TEST_ROOT/fail_write")" -eq "$address" ]; then exit 8; fi
                if [ "$address" -eq 37 ] && [ -f "$MOTOR_TEST_ROOT/mismatch" ]; then exit 0; fi
                echo "$((value & 255))" >"$MOTOR_TEST_ROOT/registers/$address"
                if [ "$transfer" = w3@0x56 ]; then
                    echo "$((value >> 8))" >"$MOTOR_TEST_ROOT/registers/$((address + 1))"
                fi
                if [ "$address" -eq 34 ]; then
                    if [ "$value" -eq 129 ] || [ "$value" -eq 130 ]; then
                        echo 112 >"$MOTOR_TEST_ROOT/registers/33"
                    else
                        echo 49 >"$MOTOR_TEST_ROOT/registers/33"
                    fi
                fi
                ;;
            *) exit 24 ;;
        esac
        ;;
    pt100_motor) echo 'Unexpected old driver invocation' >&2; exit 99 ;;
    *) exit 25 ;;
esac
EOF
chmod +x "$test_dir/bin/board-tool"
for tool in id usleep devmem i2ctransfer pt100_motor; do
    ln -s board-tool "$test_dir/bin/$tool"
done
if [ "${MOTOR_TEST_ASH:-0}" = 1 ]; then
    {
        echo '#!/bin/sh'
        cat <<'EOF'
    id() { "$MOTOR_TEST_BIN/id" "$@"; }
    devmem() { "$MOTOR_TEST_BIN/devmem" "$@"; }
    i2ctransfer() { "$MOTOR_TEST_BIN/i2ctransfer" "$@"; }
    usleep() { "$MOTOR_TEST_BIN/usleep" "$@"; }
    pt100_motor() { "$MOTOR_TEST_BIN/pt100_motor" "$@"; }
EOF
        tail -n +2 "$source_dir/sample_motor"
    } >"$sample"
    cat >"$test_dir/bin/sh" <<'EOF'
#!/bin/sh
exec "$MOTOR_TEST_BUSYBOX" sh "$@"
EOF
    chmod +x "$test_dir/bin/sh"
fi
export PATH="$test_dir/bin:$PATH"

run_sample() { sh "$sample" "$@"; }
expect_failure() {
    if run_sample "$@" >"$test_dir/result" 2>&1; then
        echo "Unexpected success: $*" >&2
        exit 1
    fi
}
wait_state() {
    wait_count=0
    while [ "$(cat "$SAMPLE_MOTOR_STATE_DIR/state" 2>/dev/null)" != "$1" ]; do
        wait_count=$((wait_count + 1))
        if [ "$wait_count" -gt 150 ]; then cat "$SAMPLE_MOTOR_STATE_DIR/motor.log" >&2; exit 1; fi
        "$MOTOR_TEST_BUSYBOX" usleep 100000
    done
}
wait_speed() {
    wait_count=0
    until grep -F "commanded=$1 pps;" "$SAMPLE_MOTOR_STATE_DIR/status" >/dev/null; do
        wait_count=$((wait_count + 1))
        if [ "$wait_count" -gt 150 ]; then cat "$SAMPLE_MOTOR_STATE_DIR/motor.log" >&2; exit 1; fi
        "$MOTOR_TEST_BUSYBOX" usleep 100000
    done
}
wait_unlock() {
    unlock_count=0
    while ! (exec 9>/tmp/pt100-motor.lock; flock -n 9); do
        unlock_count=$((unlock_count + 1))
        [ "$unlock_count" -lt 100 ] || exit 1
        "$MOTOR_TEST_BUSYBOX" usleep 20000
    done
}
reset_board() {
    wait_unlock
    rm -rf "$MOTOR_TEST_ROOT" "$SAMPLE_MOTOR_STATE_DIR"
    mkdir -p "$MOTOR_TEST_ROOT/registers"
}
assert_released() {
    wait_unlock
    [ "$(cat "$MOTOR_TEST_ROOT/registers/32")" = 7 ]
    [ "$(cat "$MOTOR_TEST_ROOT/registers/34")" = 128 ]
    [ "$(cat "$MOTOR_TEST_ROOT/gpio")" = 64 ]
}

run_sample help >/dev/null
[ ! -f "$MOTOR_TEST_ROOT/trace" ]
for invalid_speed in 0 199 511 1000 0200 -1 invalid 200000000000000000; do
    expect_failure start "$invalid_speed"
    expect_failure speed "$invalid_speed"
done
expect_failure start 510 sideways
expect_failure start 510 forward extra
expect_failure stop extra
expect_failure speed
expect_failure speed 200 extra
expect_failure probe extra
expect_failure help extra
expect_failure unknown
[ ! -f "$MOTOR_TEST_ROOT/trace" ]
MOTOR_TEST_UID=1000 expect_failure probe
expect_failure speed 405

run_sample probe >/dev/null
! grep '^W ' "$MOTOR_TEST_ROOT/trace"
[ "$(cat "$MOTOR_TEST_ROOT/gpio")" = 64 ]
before_count=$(wc -l <"$MOTOR_TEST_ROOT/trace")
exec 7>/tmp/pt100-motor.lock
flock -n 7
expect_failure start
expect_failure probe
[ "$(wc -l <"$MOTOR_TEST_ROOT/trace")" -eq "$before_count" ]
flock -u 7
exec 7>&-

reset_board
run_sample >/dev/null
expect_failure start 200 reverse
expect_failure probe
if (exec 7>/tmp/pt100-motor.lock; flock -n 7); then exit 1; fi
wait_speed 510
grep -Fx 'W 38 2188' "$MOTOR_TEST_ROOT/trace" >/dev/null
grep -Fx 'W 38 858' "$MOTOR_TEST_ROOT/trace" >/dev/null
grep -Fx 'W 37 32' "$MOTOR_TEST_ROOT/trace" >/dev/null
grep -Fx 'W 46 65535' "$MOTOR_TEST_ROOT/trace" >/dev/null
"$MOTOR_TEST_BUSYBOX" usleep 2200000
wait_state running
[ "$(grep -c '^W 34 129$' "$MOTOR_TEST_ROOT/trace")" -eq 1 ]
[ "$(grep -c '^W 40 0$' "$MOTOR_TEST_ROOT/trace")" -eq 1 ]
[ "$(grep -c '^W 42 0$' "$MOTOR_TEST_ROOT/trace")" -gt 2 ]
! grep -E '^W (6[4-9]|7[0-9]) ' "$MOTOR_TEST_ROOT/trace"
run_sample status | grep 'commanded=510 pps' >/dev/null
run_sample speed 405 >/dev/null
wait_speed 405
run_sample speed 200 >/dev/null
wait_speed 200
run_sample stop >/dev/null
assert_released
[ "$(cat "$MOTOR_TEST_ROOT/registers/2")" = 128 ]
run_sample stop >/dev/null
expect_failure speed 510

reset_board
echo 80 >"$MOTOR_TEST_ROOT/gpio"
run_sample start 200 reverse >/dev/null
grep -Fx 'W 34 130' "$MOTOR_TEST_ROOT/trace" >/dev/null
run_sample stop >/dev/null
wait_unlock
[ "$(cat "$MOTOR_TEST_ROOT/gpio")" = 80 ]

for failure in id mux direction occupied thermal i2c bad_response mismatch write; do
    reset_board
    case "$failure" in
        id) echo 0 >"$MOTOR_TEST_ROOT/registers/0" ;;
        mux) echo 0 >"$MOTOR_TEST_ROOT/mux" ;;
        direction) echo 0 >"$MOTOR_TEST_ROOT/direction" ;;
        occupied) echo 135 >"$MOTOR_TEST_ROOT/registers/64" ;;
        thermal) echo 176 >"$MOTOR_TEST_ROOT/registers/4" ;;
        i2c) touch "$MOTOR_TEST_ROOT/i2c_fail" ;;
        bad_response) touch "$MOTOR_TEST_ROOT/bad_response" ;;
        mismatch) touch "$MOTOR_TEST_ROOT/mismatch" ;;
        write) echo 37 >"$MOTOR_TEST_ROOT/fail_write" ;;
    esac
    expect_failure start 200
    wait_state failed
    wait_unlock
    ! grep -E '^W 34 (129|130)$' "$MOTOR_TEST_ROOT/trace"
    case "$failure" in
        mux|direction) ! grep '^MEM 0x05021000 32 ' "$MOTOR_TEST_ROOT/trace" ;;
        *) [ "$(cat "$MOTOR_TEST_ROOT/gpio")" = 64 ] ;;
    esac
    if [ "$failure" = write ]; then
        [ "$(grep -c '^W 37 32$' "$MOTOR_TEST_ROOT/trace")" -eq 1 ]
    fi
done

for failure in thermal ended enable i2c refresh signal; do
    reset_board
    run_sample start 200 >/dev/null
    case "$failure" in
        thermal) echo 176 >"$MOTOR_TEST_ROOT/registers/4" ;;
        ended) echo 113 >"$MOTOR_TEST_ROOT/registers/33" ;;
        enable) echo 64 >"$MOTOR_TEST_ROOT/gpio" ;;
        i2c) touch "$MOTOR_TEST_ROOT/i2c_fail" ;;
        refresh) touch "$MOTOR_TEST_ROOT/refresh_fail" ;;
        signal) kill -TERM "$(cat "$SAMPLE_MOTOR_STATE_DIR/pid")" ;;
    esac
    wait_state failed
    wait_unlock
    [ "$(cat "$MOTOR_TEST_ROOT/gpio")" = 64 ]
    [ "$(grep -c '^W 34 129$' "$MOTOR_TEST_ROOT/trace")" -eq 1 ]
    expect_failure speed 510
    expect_failure stop
done

reset_board
run_sample start 200 >/dev/null
echo 34 >"$MOTOR_TEST_ROOT/fail_write"
expect_failure stop
wait_state failed
wait_unlock
[ "$(cat "$MOTOR_TEST_ROOT/gpio")" = 64 ]
[ "$(cat "$MOTOR_TEST_ROOT/registers/32")" = 7 ]

reset_board
mkdir -m 700 "$SAMPLE_MOTOR_STATE_DIR"
echo running >"$SAMPLE_MOTOR_STATE_DIR/state"
echo 99999999 >"$SAMPLE_MOTOR_STATE_DIR/pid"
touch "$SAMPLE_MOTOR_STATE_DIR/ready"
expect_failure speed 510
expect_failure stop
expect_failure status
[ ! -f "$MOTOR_TEST_ROOT/trace" ]

echo 'PASS: standalone shell, 150/510 profile, ramp/speed/manual stop, continuous refresh, shared lock, reverse, probe, validation, faults and cleanup'
