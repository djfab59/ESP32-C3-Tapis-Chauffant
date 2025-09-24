#!/usr/bin/env python3
import argparse
import math
from dataclasses import dataclass


@dataclass
class Config:
    # Default values — adjust to match your firmware if needed
    progHourDay: int = 8
    progMinuteDay: int = 0
    progTempDay: float = 25.5
    progHourNight: int = 1
    progMinuteNight: int = 0
    progTempNight: float = 20.5
    fadeDuration: int = 120  # minutes


def smooth_step(startTemp: float, endTemp: float, startMinute: int, endMinute: int, nowMinute: int) -> float:
    # Avant/après la période
    if nowMinute < startMinute:
        print("1:nowMinute:", nowMinute," startMinute:", startMinute, " endMinute:", endMinute)
        return endTemp
    if nowMinute > endMinute:
        print("2:nowMinute:", nowMinute, " startMinute:", startMinute, " endMinute:", endMinute)
        return endTemp
    # Courbe en S (cosinus), identique au code C++
    ratio = float(nowMinute - startMinute) / float(endMinute - startMinute)
    sCurve = (1 - math.cos(ratio * math.pi)) / 2.0
    return startTemp + sCurve * (endTemp - startTemp)


def get_temp_cible(now_h: int, now_m: int, cfg: Config) -> float:
    minuteNow = now_h * 60 + now_m
    minuteDay = cfg.progHourDay * 60 + cfg.progMinuteDay
    minuteNight = cfg.progHourNight * 60 + cfg.progMinuteNight

    # Cas normal (jour sans minuit)
    if minuteDay < minuteNight:
        if minuteNow >= minuteDay and minuteNow < minuteNight:
            # Jour -> interpolation depuis la nuit
            print("jour1 minuteDay:", minuteDay, " fader:", cfg.fadeDuration, "total:", minuteDay + cfg.fadeDuration, "minuteNow:", minuteNow)
            return smooth_step(
                cfg.progTempNight,
                cfg.progTempDay,
                minuteDay,
                minuteDay + cfg.fadeDuration,
                minuteNow,
            )
        else:
            # Nuit -> interpolation depuis le jour
            print("nuit1 minuteDay:", minuteDay, " fader:", cfg.fadeDuration, "total:", minuteDay + cfg.fadeDuration, "minuteNow:", minuteNow)
            return smooth_step(
                cfg.progTempDay,
                cfg.progTempNight,
                minuteNight,
                minuteNight + cfg.fadeDuration,
                minuteNow,
            )
    else:
        # Cas qui traverse minuit
        if minuteNow >= minuteDay or minuteNow < minuteNight:
            # Jour
            print("jour2")
            end = (minuteDay + cfg.fadeDuration) % (24 * 60)
            return smooth_step(cfg.progTempNight, cfg.progTempDay, minuteDay, end, minuteNow)
        else:
            # Nuit
            print("nuit2")
            end = (minuteNight + cfg.fadeDuration) % (24 * 60)
            return smooth_step(cfg.progTempDay, cfg.progTempNight, minuteNight, end, minuteNow)


def parse_hhmmss(s: str):
    parts = s.split(":")
    if len(parts) != 3:
        raise ValueError("Format attendu HH:MM:SS")
    h, m, _ = map(int, parts)
    if not (0 <= h < 24 and 0 <= m < 60):
        raise ValueError("Heure ou minute hors plage")
    return h, m


def main():
    parser = argparse.ArgumentParser(
        description="Calcule la température cible (getTempCible) à partir d'une heure HH:MM:SS."
    )
    parser.add_argument("time", help="Heure au format HH:MM:SS, ex: 10:30:30")
    parser.add_argument("--day", default=None, help="Heure jour HH:MM (défaut 07:00)")
    parser.add_argument("--night", default=None, help="Heure nuit HH:MM (défaut 22:30)")
    parser.add_argument("--tday", type=float, default=None, help="Température jour (défaut 21.0)")
    parser.add_argument("--tnight", type=float, default=None, help="Température nuit (défaut 18.5)")
    parser.add_argument("--fade", type=int, default=None, help="Durée de fade en minutes (défaut 60)")
    args = parser.parse_args()

    cfg = Config()
    if args.day:
        dh, dm = map(int, args.day.split(":"))
        cfg.progHourDay, cfg.progMinuteDay = dh, dm
    if args.night:
        nh, nm = map(int, args.night.split(":"))
        cfg.progHourNight, cfg.progMinuteNight = nh, nm
    if args.tday is not None:
        cfg.progTempDay = args.tday
    if args.tnight is not None:
        cfg.progTempNight = args.tnight
    if args.fade is not None:
        cfg.fadeDuration = args.fade

    h, m = parse_hhmmss(args.time)
    temp = get_temp_cible(h, m, cfg)
    print(f"{temp:.2f}")


if __name__ == "__main__":
    main()

