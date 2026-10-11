import csv
import datetime
import io
import sys
import unicodedata
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "include" / "metro_horaires.h"

STATIONS = {
    "1": "Bastille", "2": "Barbès - Rochechouart", "3": "République", "3B": "Pelleport",
    "4": "Châtelet", "5": "République", "6": "Bir-Hakeim", "7": "Opéra", "7B": "Jaurès",
    "8": "République", "9": "Trocadéro", "10": "Odéon", "11": "Serge Gainsbourg",
    "12": "Abbesses", "13": "Saint-Lazare", "14": "Châtelet",
}

LINES = [
    ("1", "1", "", "La Défense", "La Défense", False),
    ("1", "1", "", "Château de Vincennes", "Château de Vincennes", False),
    ("2", "2", "", "Porte Dauphine", "Porte Dauphine", False),
    ("2", "2", "", "Nation", "Nation", False),
    ("3", "3", "", "Pont de Levallois", "Pont de Levallois", False),
    ("3", "3", "", "Gallieni", "Gallieni", False),
    ("3B", "3", "bis", "Porte des Lilas", "Porte des Lilas", False),
    ("3B", "3", "bis", "Gambetta", "Gambetta", False),
    ("4", "4", "", "Bagneux", "Bagneux", False),
    ("4", "4", "", "Porte de Clignancourt", "Porte de Clignancourt", False),
    ("5", "5", "", "Bobigny", "Bobigny", False),
    ("5", "5", "", "Place d'Italie", "Place d'Italie", False),
    ("6", "6", "", "Nation", "Nation", False),
    ("6", "6", "", "Charles de Gaulle", "Charles de Gaulle-Étoile", False),
    ("7", "7", "", "La Courneuve", "La Courneuve", False),
    ("7", "7", "", "Villejuif", "Villejuif-Louis Aragon", True),
    ("7", "7", "", "Mairie d'Ivry", "Mairie d'Ivry", True),
    ("7B", "7", "bis", "Pré-Saint-Gervais", "Pré-Saint-Gervais", False),
    ("7B", "7", "bis", "Louis Blanc", "Louis Blanc", False),
    ("8", "8", "", "Balard", "Balard", False),
    ("8", "8", "", "Pointe du Lac", "Pointe du Lac", False),
    ("9", "9", "", "Mairie de Montreuil", "Mairie de Montreuil", False),
    ("9", "9", "", "Pont de Sèvres", "Pont de Sèvres", False),
    ("10", "10", "", "Gare d'Austerlitz", "Gare d'Austerlitz", False),
    ("10", "10", "", "Boulogne", "Boulogne", False),
    ("11", "11", "", "Châtelet", "Châtelet", False),
    ("11", "11", "", "Rosny", "Rosny-Bois-Perrier", False),
    ("12", "12", "", "Mairie d'Issy", "Mairie d'Issy", False),
    ("12", "12", "", "Aubervilliers", "Mairie d'Aubervilliers", False),
    ("13", "13", "", "Châtillon", "Châtillon", False),
    ("13", "13", "", "Saint-Denis - Université", "Saint-Denis Université", True),
    ("13", "13", "", "Les Courtilles", "Les Courtilles", True),
    ("14", "14", "", "Aéroport d'Orly", "Aéroport d'Orly", False),
    ("14", "14", "", "Saint-Denis Pleyel", "Saint-Denis Pleyel", False),
]
DEFAULT = ("11", "Châtelet")
DAY_NAMES = ["lundi-jeudi", "vendredi", "samedi", "dimanche"]

def norm(text):
    text = unicodedata.normalize("NFD", text.lower())
    return "".join(ch for ch in text if ch.isalnum() and unicodedata.category(ch) != "Mn")

def read(archive, name):
    return csv.DictReader(io.TextIOWrapper(archive.open(name), encoding="utf-8-sig"))

WEEKS = 3

def pick_dates(start):
    monday = start + datetime.timedelta(days=(7 - start.weekday()) % 7)
    return [[monday + datetime.timedelta(days=7 * w + d) for d in (1, 4, 5, 6)] for w in range(WEEKS)]

def main():
    if len(sys.argv) != 2:
        sys.exit("Usage : python outils/metro.py chemin/vers/IDFM-gtfs.zip")
    archive = zipfile.ZipFile(sys.argv[1])

    routes = {}
    colors = {}
    for r in read(archive, "routes.txt"):
        if r["route_type"] == "1" and r["route_short_name"] in STATIONS:
            routes[r["route_id"]] = r["route_short_name"]
            colors[r["route_short_name"]] = (r["route_color"], r["route_text_color"])

    calendar = list(read(archive, "calendar.txt"))
    start = min(datetime.datetime.strptime(c["start_date"], "%Y%m%d").date() for c in calendar)
    dates = pick_dates(start + datetime.timedelta(days=1))
    weekdays = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"]
    days = [(w, k, day) for w, week in enumerate(dates) for k, day in enumerate(week)]
    services = defaultdict(set)
    for c in calendar:
        for w, k, day in days:
            if c["start_date"] <= day.strftime("%Y%m%d") <= c["end_date"] and c[weekdays[day.weekday()]] == "1":
                services[c["service_id"]].add((w, k))
    for c in read(archive, "calendar_dates.txt"):
        for w, k, day in days:
            if c["date"] == day.strftime("%Y%m%d"):
                if c["exception_type"] == "1":
                    services[c["service_id"]].add((w, k))
                else:
                    services[c["service_id"]].discard((w, k))

    trips = {}
    for t in read(archive, "trips.txt"):
        if t["route_id"] in routes and services.get(t["service_id"]):
            trips[t["trip_id"]] = (routes[t["route_id"]], t["direction_id"], services[t["service_id"]])

    stop_names = {s["stop_id"]: norm(s["stop_name"]) for s in read(archive, "stops.txt")}
    stations = {short: norm(name) for short, name in STATIONS.items()}

    last_stop = {}
    at_station = {}
    with archive.open("stop_times.txt") as raw:
        header = raw.readline().decode("utf-8-sig").strip().split(",")
        i_trip, i_dep, i_stop, i_seq = (header.index(k) for k in ("trip_id", "departure_time", "stop_id", "stop_sequence"))
        for line in raw:
            parts = line.decode("utf-8").rstrip("\r\n").split(",")
            trip = trips.get(parts[i_trip])
            if not trip:
                continue
            seq, name = int(parts[i_seq]), stop_names.get(parts[i_stop], "")
            if parts[i_trip] not in last_stop or seq > last_stop[parts[i_trip]][0]:
                last_stop[parts[i_trip]] = (seq, name)
            if name == stations[trip[0]]:
                h, m, s = parts[i_dep].split(":")
                at_station[parts[i_trip]] = (seq, int(h) * 3600 + int(m) * 60 + int(s))

    out = [
        "// Fichier généré par outils/metro.py — ne pas modifier à la main.",
        "// Horaires théoriques du métro, d'après les données ouvertes d'Île-de-France Mobilités.",
        f"// Jours types : {', '.join(DAY_NAMES)}, pris entre le {dates[0][0]:%d/%m/%Y} et le {dates[-1][-1]:%d/%m/%Y}.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "// Heures de passage : secondes / 2 depuis minuit du jour de service",
        "// (au-delà de 43200 = après minuit, toujours rattaché à la veille).",
    ]
    table = []
    default_index = 0
    for i, (short, number, suffix, target, shown, branch) in enumerate(LINES):
        station = STATIONS[short]
        if (short, shown) == DEFAULT:
            default_index = i
        hits = {t: v for t, v in at_station.items() if trips[t][0] == short}
        to_target = {t for t in hits if norm(target) in last_stop[t][1]}
        votes = Counter(trips[t][1] for t in to_target)
        if not votes:
            sys.exit(f"Ligne {short} : aucun train de {station} vers {target}")
        direction = votes.most_common(1)[0][0]
        counts = []
        for k in range(4):
            times = []
            for w in range(WEEKS):
                candidate = sorted(v[1] for t, v in hits.items()
                                   if trips[t][1] == direction and (w, k) in trips[t][2] and v[0] < last_stop[t][0]
                                   and (not branch or t in to_target))
                candidate = [t for j, t in enumerate(candidate) if j == 0 or t - candidate[j - 1] >= 20]
                if len(candidate) > len(times):
                    times = candidate
            counts.append(len(times))
            out.append(f"static const uint16_t kMetro{i}_{k}[] = {{" + ",".join(str(t // 2) for t in times) + "};")
            first = f"{times[0] // 3600}:{times[0] % 3600 // 60:02d}" if times else "-"
            last = f"{times[-1] // 3600}:{times[-1] % 3600 // 60:02d}" if times else "-"
            print(f"ligne {short:>3} {station:<22} -> {shown:<26} {DAY_NAMES[k]:<12} {len(times):4d} métros, de {first} à {last}")
        color, text = colors[short]
        table.append(f'  {{"{number}", "{suffix}", "{shown}", "{station}", 0x{color}, {"true" if text == "000000" else "false"}, '
                     f"{{kMetro{i}_0, kMetro{i}_1, kMetro{i}_2, kMetro{i}_3}}, {{{', '.join(map(str, counts))}}}}},")
    out += [
        "",
        "struct MetroLine {",
        "  const char *number;       // numéro dans la pastille",
        "  const char *suffix;       // \"bis\" ou vide",
        "  const char *destination;  // terminus affiché",
        "  const char *station;      // station où les horaires sont relevés",
        "  uint32_t color;           // couleur de la ligne (RRGGBB)",
        "  bool darkText;            // numéro en noir (sinon en blanc)",
        "  const uint16_t *times[4]; // lundi-jeudi, vendredi, samedi, dimanche",
        "  uint16_t count[4];",
        "};",
        "",
        "static const MetroLine kMetroLines[] = {",
        *table,
        "};",
        f"constexpr int kMetroLineCount = {len(LINES)};",
        f"constexpr int kMetroDefaultLine = {default_index};  // {DEFAULT[0]} -> {DEFAULT[1]}",
    ]
    OUT.write_text("\n".join(out) + "\n")
    print(f"{OUT} : {OUT.stat().st_size // 1024} Ko")

if __name__ == "__main__":
    main()
