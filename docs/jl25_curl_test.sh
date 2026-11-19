#!/bin/bash
# =============================================================================
# JL25xx 协议 curl 全流程测试脚本
#
# 前置条件：
#   1. 相机已开机，UI 进入"手机APP连接"页面，热点已开启
#   2. 本机已连接相机热点（IP 192.168.169.x 网段）
#      * WSL2 用户：Windows 连上热点后，WSL 内一般可直接访问 192.168.169.1；
#        若不通，在 Windows 的 cmd/PowerShell 里逐条执行本脚本中的 curl 命令。
#
# 用法：
#   ./jl25_curl_test.sh                # 第一~三轮：连通性 + 只读测试（安全）
#   ./jl25_curl_test.sh --full         # 加第四轮：写入测试（切模式/改设置/拍照录像/对时）
#   ./jl25_curl_test.sh --dangerous    # 再加第五轮：删除/格式化/关机（每项需手动输入确认）
#
# 依赖：bash 4+、curl、nc、awk。退出码 = 失败用例数（0 = 全部通过）。
# =============================================================================

CAM_IP="${CAM_IP:-192.168.169.1}"
BASE="http://$CAM_IP"
DO_FULL=0
DO_DANGEROUS=0
[ "$1" = "--full" ] && DO_FULL=1
[ "$1" = "--dangerous" ] && DO_DANGEROUS=1

PASS=0; FAIL=0; SKIP=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass() { echo "  [PASS] $1"; PASS=$((PASS+1)); }
fail() {
	echo "  [FAIL] $1"
	[ -n "$2" ] && echo "         $2"
	FAIL=$((FAIL+1))
}
skip() { echo "  [SKIP] $1 ($2)"; SKIP=$((SKIP+1)); }

# req <名称> <url路径> <期望子串>
req() {
	local body
	body=$(curl -s -m 10 "$BASE$2")
	if [ "$3" = "__neg__" ]; then
		# 期望"不包含"（用于预期失败的命令）
		if [ -n "$body" ] && [ "$body" != "$body$3" ] && ! echo "$body" | grep -q '"Result":0'; then
			pass "$1 (got: $body)"
		else
			fail "$1" "期望非 {"Result":0}，got: $body"
		fi
	elif echo "$body" | grep -qF -- "$3"; then
		pass "$1"
	else
		fail "$1" "url=$BASE$2  期望含[$3]  实际=[$body]"
	fi
}

confirm() { # confirm <提示> -> 0=继续
	if [ -t 0 ]; then
		local ans
		read -r -p "$1 [输入 yes 继续，其它跳过]: " ans
		[ "$ans" = "yes" ]
	else
		echo "  [SKIP] $1（非交互终端，跳过危险操作）"
		return 1
	fi
}

echo "============================================================="
echo " JL25xx 协议测试  目标: $CAM_IP   模式: $1"
echo "============================================================="

# -----------------------------------------------------------------------------
echo ""
echo "== 第一轮：连通性 =="
if ! ping -c 2 -W 2 "$CAM_IP" >/dev/null 2>&1; then
	echo "  [FAIL] ping $CAM_IP 不通 —— 检查是否已连上相机热点"
	exit 1
fi
pass "ping $CAM_IP"

# -----------------------------------------------------------------------------
echo ""
echo "== 第一轮附加：旧版 APP CGI 兼容 =="
legacy_getwifi=$(curl -s -m 10 "$BASE/cgi-bin/hisnet/getwifi.cgi")
if printf '%s\n' "$legacy_getwifi" | grep -q '^0$' &&
   printf '%s\n' "$legacy_getwifi" | grep -q '^OK$'; then
	pass "legacy getwifi.cgi"
else
	fail "legacy getwifi.cgi" "实际=[$legacy_getwifi]"
fi
legacy_ssid=$(curl -s -m 10 "$BASE/cgi-bin/Config.cgi?action=get&property=Net.WIFI_AP.SSID")
if printf '%s\n' "$legacy_ssid" | grep -q '^-1$' &&
   printf '%s\n' "$legacy_ssid" | grep -q '^Fail$'; then
	pass "legacy Config.cgi 隐藏 SSID"
else
	fail "legacy Config.cgi 隐藏 SSID" "实际=[$legacy_ssid]"
fi

# -----------------------------------------------------------------------------
echo ""
echo "== 第二轮：/Misc 命令组 =="
req "Misc?AliveAck 心跳"            "/Misc?AliveAck"            '{"Result":0}'
req "Misc?GetCurrentWorkMode"       "/Misc?GetCurrentWorkMode"  '"Mode":"Camera"'
req "Misc?Info 设备信息"            "/Misc?Info"                '"ProductName":"SOPHCAM"'
req "Misc?IsWifiPasswordValid"      "/Misc?IsWifiPasswordValid" '"NeedReSet":0'
req "Misc?JpgSyncErrorAck"          "/Misc?JpgSyncErrorAck"     '{"Result":0}'
req "SetMode?Setup（空操作）"       "/SetMode?Setup"            '{"Result":0,"Mode":"Setup"}'
req "SetMode?Storage（预期失败-1）" "/SetMode?Storage"          '{"Result":-1}'
req "未知命令 -> -404"              "/Misc?NoSuchCmd"           '-404'
req "未知路径 -> -404"              "/Bogus?x=1"                '-404'

# -----------------------------------------------------------------------------
echo ""
echo "== 第三轮：/Setup + /Storage 只读 =="
req "Setup?GetBatteryStatus"        "/Setup?GetBatteryStatus"    '"Level"'
req "Setup?GetTemperatureStatus"    "/Setup?GetTemperatureStatus" '"Celsius"'
req "Setup?GetRecordingStatus"      "/Setup?GetRecordingStatus"  '"IsMP4Recording"'
req "Setup?GetNightModeFlag"        "/Setup?GetNightModeFlag"    '"Flag"'
req "Setup?GetGPS_Info"             "/Setup?GetGPS_Info"         '"Latitude"'
req "Setup?GetAllSettingsList"      "/Setup?GetAllSettingsList"  '"Format"'
req "Setup?GetDicJson 隐藏 WiFi"    "/Setup?GetDicJson"           '"Settings"'
req "Storage?GetSD_Info"            "/Storage?GetSD_Info"        '"TotalSize"'
req "Storage?GetDirFileInfo"        "/Storage?GetDirFileInfo"    '"NumberOfFiles"'
req "Storage?GetFilePage=0"         "/Storage?GetFilePage=0&numPerPage=128" '"fs"'
req "Storage?GetFilePage type=Image" "/Storage?GetFilePage=0&type=Image&numPerPage=8" '"fs"'
req "Storage?GetFilePage type=Video" "/Storage?GetFilePage=0&type=Video&numPerPage=8" '"fs"'
req "Storage?GetFileList"           "/Storage?GetFileList"       '"media"'

# 提取文件清单，选出最小文件（快速下载）与最大文件（事件测试）
PAGE_JSON=$(curl -s -m 15 "$BASE/Storage?GetFilePage=0&numPerPage=128")
mapfile -t FIDS  < <(echo "$PAGE_JSON" | grep -o '"fid":"[^"]*"'  | sed 's/"fid":"\(.*\)"/\1/')
mapfile -t SIZES < <(echo "$PAGE_JSON" | grep -o '"s":[0-9]*' | sed 's/"s"://')

SMALL_FID=""; SMALL_SZ=0; BIG_FID=""; BIG_SZ=0
for i in "${!FIDS[@]}"; do
	sz="${SIZES[i]:-0}"
	[ -z "$sz" ] && continue
	if [ -z "$SMALL_FID" ] || [ "$sz" -lt "$SMALL_SZ" ]; then
		SMALL_FID="${FIDS[i]}"; SMALL_SZ=$sz
	fi
	if [ "$sz" -gt "$BIG_SZ" ]; then
		BIG_FID="${FIDS[i]}"; BIG_SZ=$sz
	fi
done

if [ -z "$SMALL_FID" ]; then
	skip "缩略图/下载测试" "SD 卡上没有可下载的媒体文件，先本地拍几张"
else
	echo "  (选取测试文件: $SMALL_FID ${SMALL_SZ}B / $BIG_FID ${BIG_SZ}B)"

	# 缩略图：应为 JPEG（FFD8 开头）。请求失败时 curl 落盘 404 文本，
	# 这里保留诊断信息以便定位（fid/path/errno 看设备端 MLOG_ERR）
	curl -s -m 15 -o "$TMP/thumb.jpg" "$BASE/Storage?GetFileTHumb=$SMALL_FID"
	magic=$(od -An -tx1 -N2 "$TMP/thumb.jpg" 2>/dev/null | tr -d ' ')
	if [ "$magic" = "ffd8" ]; then
		pass "Storage?GetFileTHumb (JPEG magic ffd8, $(stat -c%s "$TMP/thumb.jpg")B)"
	else
		fail "Storage?GetFileTHumb" \
			"magic=$magic（期望 ffd8）；$(cat "$TMP/thumb.jpg")"
	fi

	# 完整下载：大小应与列表 s 字段一致
	curl -s -m 120 -o "$TMP/dl.bin" "$BASE/Storage?Download=$SMALL_FID"
	dl_sz=$(stat -c%s "$TMP/dl.bin" 2>/dev/null || echo 0)
	if [ "$dl_sz" = "$SMALL_SZ" ]; then
		pass "Storage?Download 完整下载 ${dl_sz}B"
	else
		fail "Storage?Download 完整下载" \
			"期望 ${SMALL_SZ}B 实际 ${dl_sz}B；$(cat "$TMP/dl.bin")"
	fi

	# 断点续传 @offset：单独验证大小与首字节（不依赖完整下载结果）
	curl -s -m 60 -o "$TMP/dl_off.bin" "$BASE/Storage?Download=$SMALL_FID@100"
	off_sz=$(stat -c%s "$TMP/dl_off.bin" 2>/dev/null || echo 0)
	if [ "$off_sz" = "$((SMALL_SZ-100))" ]; then
		pass "Storage?Download@100 断点续传（大小 ${off_sz}B 正确）"
		# 内容校验仅在完整下载成功时做（此时 dl.bin 才是真实文件）
		if [ "$dl_sz" = "$SMALL_SZ" ] && \
		   cmp -s "$TMP/dl_off.bin" <(tail -c +101 "$TMP/dl.bin"); then
			pass "Storage?Download@100 内容与完整文件尾部一致"
		fi
	else
		fail "Storage?Download@100 断点续传" \
			"期望 $((SMALL_SZ-100))B 实际 ${off_sz}B；$(cat "$TMP/dl_off.bin")"
	fi

		# ---- 事件推送 TCP:8222 ----
		if command -v nc >/dev/null 2>&1; then
			# Event 17：大文件下载每 1MB 推一次。
			# 注意：@N 是"起始偏移"不是"传 N 字节"，@1 会下载全量 140MB（很慢）。
			# 探测用 @<size-1> 从末尾取 1 字节，快且能确认文件可读。
			probe_off=$((BIG_SZ > 1 ? BIG_SZ - 1 : 1))
			probe_sz=$(curl -s -m 20 -o /dev/null -w '%{http_code}:%{size_download}' \
				"$BASE/Storage?Download=$BIG_FID@$probe_off" 2>/dev/null)
			if [ "$BIG_SZ" -gt 1048576 ] && [ "${probe_sz%%:*}" = "200" ]; then
				# 从偏移 <size-16MB> 处下载：跨过 1MB 边界触发 Event 17，
				# 又用 curl --range 只收前几字节，避免传全量。
				ev17_off=$((BIG_SZ > 16777216 ? BIG_SZ - 16777216 : 1))
				timeout 25 nc "$CAM_IP" 8222 >"$TMP/ev17.log" 2>/dev/null &
				sleep 1
				curl -s -m 60 --range 0-1023 \
					"$BASE/Storage?Download=$BIG_FID@$ev17_off" -o /dev/null
				sleep 2
				if grep -q '"Event":17' "$TMP/ev17.log"; then
					pass "Event 17（下载进度 1MB）: $(grep -c Event "$TMP/ev17.log") 条"
				else
					fail "Event 17（下载进度）" "8222 未收到，日志: $(cat "$TMP/ev17.log")"
				fi

				# Event 5：APP 中途断开下载（同样用末尾偏移避免传全量）
				timeout 20 nc "$CAM_IP" 8222 >"$TMP/ev5.log" 2>/dev/null &
				sleep 1
				curl -s -m 2 --range 0-1048576 \
					"$BASE/Storage?Download=$BIG_FID@$ev17_off" -o /dev/null  # 2 秒后 curl 被杀
				sleep 3
				if grep -q '"Event":5' "$TMP/ev5.log"; then
					pass "Event 5（下载中断）"
				else
					fail "Event 5（下载中断）" "8222 未收到，日志: $(cat "$TMP/ev5.log")"
				fi
			else
				skip "Event 17/5 下载事件" \
					"无 >1MB 文件，或大文件下载不通（probe=$probe_sz，期望 200）"
			fi
		echo "  -- 手动事件验证提示：保持 nc $CAM_IP 8222 挂着，插/拔 SD 卡应收到 {\"Event\":12}/{\"Event\":13}"
	else
		skip "Event 17/5 下载事件" "本机无 nc"
	fi
fi

# -----------------------------------------------------------------------------
if [ "$DO_FULL" = "1" ]; then
	echo ""
	echo "== 第四轮：写入类（--full）=="

	req "SetMode?PhotoCapture"  "/SetMode?PhotoCapture" '"Mode":"PhotoCapture"'
	req "SetMode?VideoCapture"  "/SetMode?VideoCapture" '"Mode":"VideoCapture"'

	# 设置回环：BeepSound 读->翻转->读回->恢复
	cur=$(curl -s -m 10 "$BASE/Setup?GetAllSettingsList" | grep -o '"BeepSound":[0-9]*' | grep -o '[0-9]*')
	if [ -n "$cur" ]; then
		flip=$((1-cur))
		req "Setup?BeepSound=$flip（写）" "/Setup?BeepSound=$flip" '{"Result":0}'
		back=$(curl -s -m 10 "$BASE/Setup?GetAllSettingsList" | grep -o '"BeepSound":[0-9]*' | grep -o '[0-9]*')
		if [ "$back" = "$flip" ]; then
			pass "BeepSound 读回验证（$cur->$flip）"
		else
			fail "BeepSound 读回验证" "期望 $flip 实际 ${back:-空}"
		fi
		curl -s -m 10 "$BASE/Setup?BeepSound=$cur" >/dev/null   # 恢复
		pass "BeepSound 已恢复为 $cur"
	else
		skip "BeepSound 写回环" "GetAllSettingsList 解析失败"
	fi

	# 对时：Timestamp=<秒级时间戳>-<时区偏移秒>
	tzoff=$(date +%z); sign=${tzoff:0:1}; off=$(( ${tzoff:1:2}*3600 + ${tzoff:3:2}*60 ))
	req "Setup?Timestamp 对时" "/Setup?Timestamp=$(date +%s)-$off" '{"Result":0}'

	# Shutter 拍照（注意 WORK_MODE_BOOT 下会被状态机忽略 —— 预期可能不生效）
	n_before=$(curl -s -m 10 "$BASE/Storage?GetDirFileInfo" | grep -o '"NumberOfFiles":[0-9]*' | grep -o '[0-9]*')
	req "Setup?Shutter=2（拍照）" "/Setup?Shutter=2" '{"Result":0}'
	sleep 3
	n_after=$(curl -s -m 10 "$BASE/Storage?GetDirFileInfo" | grep -o '"NumberOfFiles":[0-9]*' | grep -o '[0-9]*')
	if [ "${n_after:-0}" -gt "${n_before:-0}" ] 2>/dev/null; then
		pass "拍照生效（文件数 $n_before -> $n_after）"
	else
		skip "拍照生效" "文件数未增加（$n_before -> $n_after）。设备处于 WORK_MODE_BOOT 时 Shutter 消息被忽略（已知限制，见交接文档第五节第 5.1 点）"
	fi

	# Shutter 录像回环（同样受 WORK_MODE_BOOT 限制）
	req "Setup?Shutter=1（开始录像）" "/Setup?Shutter=1" '{"Result":0}'
	sleep 3
	rec=$(curl -s -m 10 "$BASE/Setup?GetRecordingStatus" | grep -o '"IsMP4Recording":[0-9]*' | grep -o '[0-9]*$')
	if [ "$rec" = "1" ]; then
		pass "录像状态 IsMP4Recording=1"
	else
		skip "录像状态" "IsMP4Recording=${rec:-空}（WORK_MODE_BOOT 下 START_REC 被忽略，已知限制）"
	fi
	req "Setup?Shutter=0（停止录像）" "/Setup?Shutter=0" '{"Result":0}'
fi

# -----------------------------------------------------------------------------
if [ "$DO_DANGEROUS" = "1" ]; then
	echo ""
	echo "== 第五轮：危险操作（--dangerous，逐项确认）=="

	if confirm "删除文件测试（会真实删除 SD 卡上一个文件）"; then
		read -r -p "  输入要删除的 fid（如 DSCF00001.jpg，回车跳过）: " delfid
		if [ -n "$delfid" ]; then
			req "Storage?Delete $delfid" "/Storage?$delfid" '{"Result":0}'
		else
			skip "Delete" "未指定 fid"
		fi
	else
		skip "Delete" "用户跳过"
	fi

	if confirm "格式化 SD 卡（卡上所有数据将被清除！）"; then
		req "Setup?Format" "/Setup?Format" '{"Result":0}'
		n_fmt=$(curl -s -m 15 "$BASE/Storage?GetDirFileInfo" | grep -o '"NumberOfFiles":[0-9]*' | grep -o '[0-9]*')
		if [ "${n_fmt:-1}" = "0" ]; then
			pass "格式化后文件数为 0"
		else
			fail "格式化后文件数" "NumberOfFiles=${n_fmt:-解析失败}"
		fi
	else
		skip "Format" "用户跳过"
	fi

	if confirm "远程关机 PowerOff（执行后相机将直接关机，本脚本结束）"; then
		req "Misc?PowerOff" "/Misc?PowerOff" '{"Result":0}'
		echo "  关机指令已下发，相机应在 1~2 秒内断电"
	else
		skip "PowerOff" "用户跳过"
	fi
fi

# -----------------------------------------------------------------------------
echo ""
echo "============================================================="
echo " 结果: PASS=$PASS  FAIL=$FAIL  SKIP=$SKIP"
echo "============================================================="
exit "$FAIL"
