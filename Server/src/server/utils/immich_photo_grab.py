from dataclasses import dataclass
from typing import Callable
from pathlib import Path
from PIL import Image
from PIL.ImageFile import ImageFile
from reportlab.lib.units import inch
import requests
import random
import os
import io


@dataclass
class ImmichPicGrab:
    cacheStore: Path
    photoAlbumID: str
    immichAPIURL: str
    immichAPIKey: str
    compressionFunction: Callable[[ImageFile], Image.Image]

    def getRandomPhoto(self) -> Path | None:
        album = self.getAlbumContents()
        selection = random.choice(album)
        return self.grabPhoto(selection.get("id"))

    def getAlbumContents(self):
        payload = {}
        headers = {"Accept": "application/json", "x-api-key": self.immichAPIKey}

        response = requests.request(
            "GET", self.albumURL(), headers=headers, data=payload
        )

        return response.json()["assets"]

    def albumURL(self):
        return f"{self.immichAPIURL}/albums/{self.photoAlbumID}"

    def cacheName(self, photo_id: str):
        return f"{self.compressionFunction.__qualname__}_{photo_id}.jpg"

    def isInCache(self, photo_id: str):
        loc = self.fileCachePath(photo_id)
        return loc if os.path.exists(loc) else None

    def fileCachePath(self, photo_id: str):
        return Path(os.path.join(self.cacheStore, self.cacheName(photo_id)))

    def writeToCache(self, photo, photo_id):
        photo.raise_for_status()

        os.makedirs(self.cacheStore, exist_ok=True)
        with io.BytesIO() as buffer:
            for chunk in photo.iter_content(chunk_size=8192):
                buffer.write(chunk)
            buffer.seek(0)

            img = self.compressionFunction(Image.open(buffer)).convert("RGB")

            img.save(self.fileCachePath(photo_id), format="JPEG")

        return self.fileCachePath(photo_id)

    def requestPhotoFromImmich(self, photo_id: str):
        url = f"{self.immichAPIURL}/assets/{photo_id}/thumbnail"

        payload = {}
        headers = {"Accept": "application/octet-stream", "x-api-key": self.immichAPIKey}

        response = requests.request(
            "GET", url, headers=headers, data=payload, stream=True
        )
        return self.writeToCache(response, photo_id)

    def grabPhoto(self, photo_id: str):
        if not self.isInCache(photo_id):
            self.requestPhotoFromImmich(photo_id)
        return self.isInCache(photo_id)


@dataclass
class ImageModifiers:
    @staticmethod
    def resize500x500(photo: ImageFile) -> Image.Image:
        return photo.resize((500, 500), Image.Resampling.LANCZOS)

    @staticmethod
    def resizeCassiePage(photo: ImageFile):
        f_width = 2.167 * inch * 5
        f_height = 2.25 * inch * 5
        r_width, r_height = photo.size
        width_ratio = f_width / r_width
        height_ratio = f_height / r_height

        scaling_ratio = max(width_ratio, height_ratio)
        new_size = photo.resize(
            (int(r_width * scaling_ratio), int(r_height * scaling_ratio))
        )

        left = (int(r_width * scaling_ratio) - f_width) / 2
        top = (int(r_height * scaling_ratio) - f_height) / 2
        right = (int(r_width * scaling_ratio) + f_width) / 2
        bottom = (int(r_height * scaling_ratio) + f_height) / 2

        return new_size.crop((left, top, right, bottom))
