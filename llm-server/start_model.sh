#!/bin/bash
# =============================================================
# start_model.sh — 选择模型并启动 LLM 服务链路
# 用法:
#   ./start_model.sh                    # 交互式选择
#   ./start_model.sh ollama-qwen2.5     # 指定模型
#   ./start_model.sh ollama-qwen3
#   ./start_model.sh vllm-awq
#
# 可选模型:
#   ollama-qwen2.5   -> Ollama  / qwen2.5:7b-instruct-q4_K_M (11434)
#   ollama-qwen3     -> Ollama  / qwen3:8b                    (11434)
#   vllm-awq         -> vLLM    / Qwen2.5-7B-Instruct-AWQ      (8000)
# =============================================================
set -u

SERVER_DIR=/home/yc_21/server_ddz
LLM_DIR=$SERVER_DIR/llm-server
BIN=$SERVER_DIR/bin/llm-server
LOG_DIR=$SERVER_DIR/logs
OLLAMA_MODELS_CONFIG=$LLM_DIR/config.ollama.json
VLLM_MODELS_CONFIG=$LLM_DIR/config.vllm.json
OLLAMA_PORT=11434
VLLM_PORT=8000

# ---- vLLM 相关（写死，切换时可改这里）----
VLLM_BIN=/home/yc_21/miniconda3/envs/qwen/bin/vllm
VLLM_MODEL_DIR=/mnt/c/Users/yc_21/Qwen2.5-7B-Instruct-AWQ
VLLM_MODEL_NAME=Qwen2.5-7B-Instruct-AWQ

log()  { echo "[start_model] $*"; }
die()  { echo "[start_model] ERROR: $*" >&2; exit 1; }

# ---------------- 选择模型 ----------------
pick_model() {
    echo "可选模型:"
    echo "  1) ollama-qwen2.5   (Ollama  qwen2.5:7b-instruct-q4_K_M)"
    echo "  2) ollama-qwen3     (Ollama  qwen3:8b)"
    echo "  3) vllm-awq         (vLLM    Qwen2.5-7B-Instruct-AWQ)"
    printf "请输入序号 [1-3]: "
    read -r sel
    case "$sel" in
        1) choice="ollama-qwen2.5" ;;
        2) choice="ollama-qwen3" ;;
        3) choice="vllm-awq" ;;
        *) die "无效选择: $sel" ;;
    esac
}

case "${1:-}" in
    ollama-qwen2.5|ollama-qwen3|vllm-awq) choice="$1" ;;
    "") pick_model ;;
    *) die "未知模型: $1。可用: ollama-qwen2.5 / ollama-qwen3 / vllm-awq" ;;
esac

# ---------------- 按选择确定上游 ----------------
case "$choice" in
    ollama-qwen2.5)
        BACKEND="ollama"
        MODEL_NAME="qwen2.5:7b-instruct-q4_K_M"
        UPSTREAM_PORT=$OLLAMA_PORT
        CFG_SRC=$OLLAMA_MODELS_CONFIG
        LATEST_CFG=$LLM_DIR/config.ollama.json
        ;;
    ollama-qwen3)
        BACKEND="ollama"
        MODEL_NAME="qwen3:8b"
        UPSTREAM_PORT=$OLLAMA_PORT
        CFG_SRC=$OLLAMA_MODELS_CONFIG
        LATEST_CFG=$LLM_DIR/config.ollama.json
        ;;
    vllm-awq)
        BACKEND="vllm"
        MODEL_NAME="$VLLM_MODEL_NAME"
        UPSTREAM_PORT=$VLLM_PORT
        CFG_SRC=$VLLM_MODELS_CONFIG
        LATEST_CFG=$LLM_DIR/config.vllm.json
        ;;
esac

log "选择: $choice -> $BACKEND / $MODEL_NAME (upstream 127.0.0.1:$UPSTREAM_PORT)"

# =====================================================
# 1. 确保上游模型服务在跑
# =====================================================
ensure_ollama() {
    if curl -s -m 2 "http://127.0.0.1:$OLLAMA_PORT/api/version" >/dev/null 2>&1; then
        log "Ollama 已运行 (port $OLLAMA_PORT)"
        # 顺便确认模型存在
        tags=$(curl -s -m 5 "http://127.0.0.1:$OLLAMA_PORT/api/tags" 2>/dev/null)
        if ! echo "$tags" | grep -q "$1"; then
            die "Ollama 里找不到模型 [$1]。请先: ollama pull $1"
        fi
        log "模型 [$1] 已存在"
    else
        die "Ollama 未运行。请先启动: ollama serve"
    fi
}

ensure_vllm() {
    if curl -s -m 2 "http://127.0.0.1:$VLLM_PORT/v1/models" >/dev/null 2>&1; then
        log "vLLM 已运行 (port $VLLM_PORT)"
        return
    fi
    log "vLLM 未运行, 启动中 ..."
    mkdir -p "$LOG_DIR"
    cd "$(dirname "$VLLM_MODEL_DIR")"   # 兼容相对路径
    # WSL2 下 vLLM 默认禁用 pinned memory -> V1 引擎 UVA 检查失败
    # (RuntimeError: UVA is not available)。显式开启:
    VLLM_WSL2_ENABLE_PIN_MEMORY=1 nohup "$VLLM_BIN" serve "$VLLM_MODEL_DIR" \
        --host 0.0.0.0 \
        --port $VLLM_PORT \
        --served-model-name "$VLLM_MODEL_NAME" \
        --quantization awq \
        --max-model-len 4096 \
        --gpu-memory-utilization 0.85 \
        --trust-remote-code \
        --enforce-eager \
        > "$LOG_DIR/vllm.log" 2>&1 &
    log "vLLM 后台启动, 日志: $LOG_DIR/vllm.log, 等待就绪 (模型加载可能要几分钟) ..."
    for i in $(seq 1 60); do
        if curl -s -m 2 "http://127.0.0.1:$VLLM_PORT/v1/models" >/dev/null 2>&1; then
            log "vLLM 就绪 (等待 ${i}x3s)"
            return
        fi
        sleep 3
    done
    die "vLLM 启动超时, 检查 $LOG_DIR/vllm.log"
}

case "$BACKEND" in
    ollama) ensure_ollama "$MODEL_NAME" ;;
    vllm)   ensure_vllm ;;
esac

# =====================================================
# 2. 生成/更新 llm-server 生效配置 (config.json)
# =====================================================
# main.cpp 默认读 config.ollama.json; 为了让用户"当前选的是啥"清晰,
# 我们把所选配置同步到 config.json, 并在 config.json 里写当前 model
ACTIVE_CFG=$LLM_DIR/config.json
# 基础复制 (config.json 作为"当前生效"镜像，也方便 --config 指定)
cp "$CFG_SRC" "$ACTIVE_CFG"
sed -i "s|\"model\"[[:space:]]*:[[:space:]]*\"[^\"]*\"|\"model\": \"$MODEL_NAME\"|" "$ACTIVE_CFG"
sed -i "s|\"upstream_url\"[[:space:]]*:[[:space:]]*\"[^\"]*\"|\"upstream_url\": \"http://127.0.0.1:$UPSTREAM_PORT\"|" "$ACTIVE_CFG"
log "已生成生效配置: $ACTIVE_CFG"
log "model=$MODEL_NAME upstream=127.0.0.1:$UPSTREAM_PORT"

# =====================================================
# 3. 重启 llm-server
# =====================================================
pid=$(pgrep -x llm-server | head -1)
if [ -n "$pid" ]; then
    log "停止旧 llm-server (pid=$pid)"
    kill "$pid" 2>/dev/null
    sleep 1
fi

cd "$SERVER_DIR"
mkdir -p "$LOG_DIR"
nohup stdbuf -oL -eL "$BIN" --config "$ACTIVE_CFG" > "$LOG_DIR/llm-server.log" 2>&1 &
sleep 1
log "llm-server 已启动 (pid=$!), 日志: $LOG_DIR/llm-server.log"

# 验证
if pgrep -x llm-server >/dev/null; then
    log "OK: llm-server 正在监听 9000, 后端=$BACKEND 模型=$MODEL_NAME"
    echo
    echo "  测试: curl -N -X POST http://127.0.0.1:9000/v1/chat/completions \\"
    echo "           -H 'Content-Type: application/json' \\"
    echo "           -d '{\"prompt\":\"你好\",\"history\":[],\"system_prompt\":\"\"}'"
else
    die "llm-server 启动失败, 查看 $LOG_DIR/llm-server.log"
fi