from fastapi import FastAPI, Request
from pydantic import BaseModel
from src.server.utils.map_tools import MapEvent, PlotPoint, dummyMapEvent
from src.server.utils.print_pdf import printPdf
from src.server.utils.page_gen import gen_cassie_pdf
from fastapi import Response, BackgroundTasks
import logging
import os

TRACCAR_SERVER = os.environ["TRACCAR_SERVER"]

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI()


class LocationReport(BaseModel):
    event: MapEvent
    publishTraccar: bool = False
    printMap: bool = False
    returnPDF: bool = False


@app.middleware("http")
async def log_requests(request: Request, call_next):
    # Log request details
    logger.info(f"Request URL: {request.url}")
    logger.info(f"Request Method: {request.method}")
    logger.info(f"Request Headers: {request.headers}")

    # Read the request body
    body = await request.body()
    logger.info(f"Request Body: {body.decode('utf-8')}")

    # Process the request
    response = await call_next(request)
    return response


@app.post("/location/")
async def create_item(background_tasks: BackgroundTasks, report: LocationReport):
    print(report)
    if report.publishTraccar:
        report.event.pushAll(TRACCAR_SERVER)
    if report.printMap or report.returnPDF:
        buffer = gen_cassie_pdf(report.event)
        background_tasks.add_task(buffer.close)
        if report.printMap:
            printPdf(buffer)
        if report.returnPDF:
            headers: dict[str, str] = {
                "Content-Disposition": 'inline; filename="out.pdf"'
            }
            return Response(
                buffer.getvalue(), headers=headers, media_type="application/pdf"
            )
    return {"status": "success"}


@app.get("/location/dummyData")
async def dummyReturn():
    return dummyMapEvent()
