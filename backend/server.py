from fastapi import FastAPI, APIRouter, HTTPException
from fastapi.responses import StreamingResponse
from dotenv import load_dotenv
from starlette.middleware.cors import CORSMiddleware
from motor.motor_asyncio import AsyncIOMotorClient
import os
import json
import logging
import uuid
from pathlib import Path
from pydantic import BaseModel, Field, ConfigDict
from typing import List, Optional
from datetime import datetime, timezone

from emergentintegrations.llm.chat import LlmChat, UserMessage, TextDelta, StreamDone


ROOT_DIR = Path(__file__).parent
load_dotenv(ROOT_DIR / '.env')

# MongoDB connection
mongo_url = os.environ['MONGO_URL']
client = AsyncIOMotorClient(mongo_url)
db = client[os.environ['DB_NAME']]

# ------------------- LLM config -------------------
EMERGENT_LLM_KEY = os.environ.get("EMERGENT_LLM_KEY")
AI_PROVIDER = "openai"
AI_MODEL = "gpt-5.6-terra"  # "Terra" variant chosen by user; 5.2 Terra does not exist

SYSTEM_PROMPT_DIAG = (
    "You are an expert Modbus RTU / RS-485 industrial automation engineer helping a technician "
    "diagnose a Modbus network scanned from an ESP32-based scanner. "
    "You will receive JSON with scanned slave IDs and/or register values (holding/input/coil/discrete). "
    "Reply in Polish. Be concise, use short bullet points. For each device: suggest the most likely "
    "device type (energy meter, inverter, PLC, HVAC controller, temperature transmitter, VFD, etc.) "
    "based on slave ID conventions, baudrate, and register patterns. For register dumps: suggest the "
    "likely meaning of each register (scaling, data type, unit) and highlight anomalies. "
    "If the data is insufficient say so and ask for a specific additional read."
)

SYSTEM_PROMPT_NLCMD = (
    "You translate natural-language instructions from a technician into Modbus RTU read operations. "
    "Return ONLY a JSON object (no prose, no markdown fences) with this exact schema: "
    '{"slaveId": <int 1-247>, "regType": "holding"|"input"|"coil"|"discrete", '
    '"startAddr": <int 0-65535>, "count": <int 1-125>, "explanation": "<short Polish rationale>"}. '
    "If the request is ambiguous, pick the most common convention for that device class and "
    "explain your choice in the explanation field. Never output text outside the JSON."
)

# ------------------- FastAPI -------------------
app = FastAPI(title="Modbus Scanner Companion API")
api_router = APIRouter(prefix="/api")


# ------------------- Health -------------------
class StatusCheck(BaseModel):
    model_config = ConfigDict(extra="ignore")
    id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    client_name: str
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))


class StatusCheckCreate(BaseModel):
    client_name: str


@api_router.get("/")
async def root():
    return {"service": "Modbus Scanner Companion", "model": AI_MODEL}


@api_router.post("/status", response_model=StatusCheck)
async def create_status_check(input: StatusCheckCreate):
    status_obj = StatusCheck(**input.model_dump())
    doc = status_obj.model_dump()
    doc['timestamp'] = doc['timestamp'].isoformat()
    await db.status_checks.insert_one(doc)
    return status_obj


@api_router.get("/status", response_model=List[StatusCheck])
async def get_status_checks():
    rows = await db.status_checks.find({}, {"_id": 0}).to_list(1000)
    for r in rows:
        if isinstance(r['timestamp'], str):
            r['timestamp'] = datetime.fromisoformat(r['timestamp'])
    return rows


# ------------------- AI models -------------------
class AiDeviceInfo(BaseModel):
    id: int
    baud: Optional[int] = None
    parity: Optional[int] = None
    stop: Optional[int] = None


class AiRegisterValue(BaseModel):
    addr: int
    value: int
    ok: bool = True


class AiChatRequest(BaseModel):
    session_id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    prompt: str
    devices: Optional[List[AiDeviceInfo]] = None
    registers: Optional[List[AiRegisterValue]] = None


class AiAnalyzeRequest(BaseModel):
    session_id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    devices: List[AiDeviceInfo]
    baud: Optional[int] = None


class AiNlCommandRequest(BaseModel):
    session_id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    text: str
    known_devices: Optional[List[AiDeviceInfo]] = None


# ------------------- AI helpers -------------------
def _ensure_key():
    if not EMERGENT_LLM_KEY:
        raise HTTPException(status_code=500, detail="EMERGENT_LLM_KEY not configured")


def _build_chat(session_id: str, system_message: str) -> LlmChat:
    _ensure_key()
    chat = LlmChat(
        api_key=EMERGENT_LLM_KEY,
        session_id=session_id,
        system_message=system_message,
    ).with_model(AI_PROVIDER, AI_MODEL)
    return chat


async def _stream_sse(chat: LlmChat, user_text: str):
    async def gen():
        try:
            async for ev in chat.stream_message(UserMessage(text=user_text)):
                if isinstance(ev, TextDelta):
                    data = json.dumps({"delta": ev.content})
                    yield f"data: {data}\n\n"
                elif isinstance(ev, StreamDone):
                    yield "data: {\"done\": true}\n\n"
                    break
        except Exception as e:
            err = json.dumps({"error": str(e)})
            yield f"data: {err}\n\n"
    return StreamingResponse(
        gen(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no", "Connection": "keep-alive"},
    )


# ------------------- AI endpoints -------------------
@api_router.post("/ai/chat")
async def ai_chat(req: AiChatRequest):
    """Streaming assistant: user prompt + optional Modbus context."""
    ctx_parts: List[str] = []
    if req.devices:
        ctx_parts.append("Scanned devices:\n" + json.dumps([d.model_dump() for d in req.devices], ensure_ascii=False))
    if req.registers:
        ctx_parts.append("Register readings:\n" + json.dumps([r.model_dump() for r in req.registers], ensure_ascii=False))
    context = "\n\n".join(ctx_parts)
    user_text = f"{context}\n\nUser question:\n{req.prompt}" if context else req.prompt

    chat = _build_chat(req.session_id, SYSTEM_PROMPT_DIAG)
    return await _stream_sse(chat, user_text)


@api_router.post("/ai/analyze-scan")
async def ai_analyze_scan(req: AiAnalyzeRequest):
    """Automatic analysis of a scan result. Streams a Polish diagnostic report."""
    if not req.devices:
        raise HTTPException(400, "no devices provided")
    user_text = (
        f"Zeskanowano magistralę Modbus RTU przy prędkości {req.baud or 'nieznanej'} baud. "
        f"Lista znalezionych urządzeń (JSON):\n"
        f"{json.dumps([d.model_dump() for d in req.devices], ensure_ascii=False)}\n\n"
        "Przygotuj krótki raport: dla każdego ID podaj 2-3 najbardziej prawdopodobne typy urządzenia "
        "(zgodnie z typowymi konwencjami adresacji), zasugeruj od którego rejestru zacząć diagnostykę "
        "(np. 'odczytaj Holding 0..10 aby zidentyfikować model') i zaznacz czy liczba urządzeń jest typowa."
    )
    chat = _build_chat(req.session_id, SYSTEM_PROMPT_DIAG)
    return await _stream_sse(chat, user_text)


@api_router.post("/ai/nl-command")
async def ai_nl_command(req: AiNlCommandRequest):
    """Non-streaming: translate natural language to a Modbus read command (JSON)."""
    _ensure_key()
    ctx = ""
    if req.known_devices:
        ctx = "\nKnown devices on bus: " + json.dumps(
            [d.model_dump() for d in req.known_devices], ensure_ascii=False)
    chat = _build_chat(req.session_id, SYSTEM_PROMPT_NLCMD)
    reply = await chat.send_message(UserMessage(text=req.text + ctx))
    # Try to parse JSON; strip ```json fences if any
    raw = (reply or "").strip()
    if raw.startswith("```"):
        raw = raw.strip("`")
        if raw.lower().startswith("json"):
            raw = raw[4:].lstrip()
    try:
        parsed = json.loads(raw)
    except Exception:
        raise HTTPException(422, f"Model did not return JSON. Raw: {raw[:300]}")
    return parsed


# Mount
app.include_router(api_router)

app.add_middleware(
    CORSMiddleware,
    allow_credentials=True,
    allow_origins=os.environ.get('CORS_ORIGINS', '*').split(','),
    allow_methods=["*"],
    allow_headers=["*"],
)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


@app.on_event("shutdown")
async def shutdown_db_client():
    client.close()
