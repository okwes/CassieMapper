from dataclasses import dataclass
from datetime import datetime, timedelta
import math
import staticmaps
import requests
import logging
from pydantic import BaseModel

logger: logging.Logger = logging.getLogger(__name__)


class PlotPoint(BaseModel):
    # A point on a Map!

    time: datetime
    lat: float
    lon: float
    acc: float = 2.0
    speed: float = 0
    head: float = 0
    alt: float = 0

    def getDistance(self, other: "PlotPoint"):
        # Uses the haversine formula, good old 131 days

        R = 3959  # Radius of Earth in miles

        dLat = math.radians(float(other.lat) - float(self.lat))
        dLon = math.radians(float(other.lon) - float(self.lon))
        lat1 = math.radians(float(self.lat))
        lat2 = math.radians(float(other.lat))

        a = math.sin(dLat / 2) * math.sin(dLat / 2) + math.cos(lat1) * math.cos(
            lat2
        ) * math.sin(dLon / 2) * math.sin(dLon / 2)

        c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))

        return R * c

    def pushToTraccar(self, url: str, deviceId: str):
        data = {
            "location": {
                "timestamp": self.time.strftime(
                    "%Y-%m-%dT%H:%M:%SZ"
                ),  # Traccar doesn't seem to properly use timezones in ISO standard, but almost everyone does it wrong, this caused a headache...
                "coords": {
                    "latitude": self.lat,  # latitude in degrees
                    "longitude": self.lon,  # longitude in degrees
                    "accuracy": self.acc,  # accuracy in meters
                    "speed": self.speed,  # speed in m/s
                    "heading": self.head,  # heading in degrees
                    "altitude": self.alt,  # altitude in meters
                },
                "is_moving": False,  # motion state
                "event": "motionchange",  # event type
                "battery": {
                    "level": 1,  # battery level as a decimal value from 0 to 1
                    "is_charging": False,  # charging state
                },
                "activity": {
                    "type": "still"  # activity type; examples are 'still', 'walking', 'in_vehicle'
                },
                "extras": {},
            },
            "device_id": deviceId,  # device identifier
        }
        logger.info(data)

        headers = {"Content-Type": "application/json"}
        response = requests.post(url, json=data, headers=headers)

        return response


class MapEvent(BaseModel):
    points: list[PlotPoint]
    device_id: str

    def getTimeDelta(self):
        times = list(map(lambda point: point.time, self.points))
        return max(times) - min(times)

    def distanceTraveled(self):
        sorted_by_times = sorted(
            self.points, key=lambda point: point.time, reverse=True
        )
        # sometiems i like python
        return sum(
            start.getDistance(end)
            for start, end in zip(sorted_by_times, sorted_by_times[1:])
        )

    def pushAll(self, url: str):
        for point in self.points:
            status = point.pushToTraccar(url, self.device_id)
            if status.status_code != 200:
                logger.error(
                    f"Did not receive +200 ok, status of upload of point {point} returned {status.status_code}. Full response {status}"
                )


def dummyMapEvent():
    # little function for testing
    start_time = datetime(2025, 7, 21, 10, 0, 0)  # Starting at 10:00 AM
    points = [
        PlotPoint(time=start_time, lat=40.7128, lon=-74.0060),  # Times Square
        PlotPoint(
            time=start_time + timedelta(minutes=10), lat=40.7580, lon=-73.9855
        ),  # Central Park
        PlotPoint(
            time=start_time + timedelta(minutes=20), lat=40.7306, lon=-73.9352
        ),  # East Village
        PlotPoint(
            time=start_time + timedelta(minutes=30), lat=40.7061, lon=-74.0089
        ),  # Financial District
        PlotPoint(
            time=start_time + timedelta(minutes=40), lat=40.6892, lon=-74.0445
        ),  # Statue of Liberty
        PlotPoint(
            time=start_time + timedelta(minutes=50), lat=40.748817, lon=-73.985428
        ),  # Empire State Building
    ]

    return MapEvent(points=points, device_id="dummy_device")


def eventToSVG(event: MapEvent, x: int, y: int):
    context = staticmaps.Context()
    context.set_tile_provider(staticmaps.tile_provider_OSM)
    if len(event.points) > 1:
        context.add_object(
            staticmaps.Line(
                [staticmaps.create_latlng(p.lat, p.lon) for p in event.points],
                color=staticmaps.BROWN,
                width=10,
            )
        )
    sorted_by_times = sorted(event.points, key=lambda point: point.time, reverse=True)
    context.add_object(
        staticmaps.Marker(
            staticmaps.create_latlng(sorted_by_times[0].lat, sorted_by_times[0].lon),
            size=20,
        )
    )
    context.add_object(
        staticmaps.Marker(
            staticmaps.create_latlng(
                sorted_by_times[len(sorted_by_times) - 1].lat,
                sorted_by_times[len(sorted_by_times) - 1].lon,
            ),
            size=20,
        )
    )
    return context.render_svg(x, y)
