import cups
import tempfile
from io import BytesIO
import logging
import os

logger: logging.Logger = logging.getLogger(__name__)

CUPS_PRINTER_NAME = os.environ["CUPS_PRINTER_NAME"]


def printPdf(buffer: BytesIO):
    with tempfile.NamedTemporaryFile(
        delete=True, suffix=".pdf", mode="wb"
    ) as temp_file:
        if buffer.getvalue():
            temp_file.write(buffer.getvalue())
            temp_file.flush()
            conn = cups.Connection()
            conn.printFile(
                CUPS_PRINTER_NAME,
                temp_file.name,
                "CassiePrint",
                {"sides": "two-sided-long-edge"},
            )
            logger.info(f"Printed file to {CUPS_PRINTER_NAME}")
        else:
            logger.error("PrintPDF Called with empty Buffer")
