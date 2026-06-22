import json
import random
import sys
import time
from pathlib import Path


PRESET_DIR: Path = Path(__file__).resolve().parent
POPULATION_DIR: Path = PRESET_DIR / "population"

DEFAULT_POPULATION_COUNT: int = 5
MIN_SEED: int = -2**31
MAX_SEED: int = 2**31-1

def find_candidates(filename: str) -> list[Path]:
    pattern = "*" + filename + "*.json"
    files = []
    for file in PRESET_DIR.rglob(pattern):
        if file.is_file() and file.parent != POPULATION_DIR:
            files.append(file)
    return files


def populate(file: Path, count: int) -> None:
    try:
        original: dict[str, Any] = json.loads(file.read_text(encoding="utf-8"))
    except:
        print(f"Couldn't read {file.name}")
        return
    
    if original.get("seed", None) == None:
        print(f"{file.name} doesn't have the field \"seed\"")
        return

    print(f"Populating preset {file.name} with {count} copies")

    if not POPULATION_DIR.exists():
        POPULATION_DIR.mkdir()

    name_base = file.name[:-len(".json")]
    print(name_base)

    random.seed(time.time())
    for i in range(count):
        new_seed = random.randint(MIN_SEED, MAX_SEED)
        
        new_json = original
        new_json["seed"] = new_seed
        
        new_name = f"{name_base}_{new_seed}.json"

        new_file = POPULATION_DIR / new_name
        new_file.write_text(
            json.dumps(new_json, indent=2, ensure_ascii=False),
            encoding="utf-8"
        )

    return


def main() -> None:
    if not 2 <= len(sys.argv) <= 3:
        print("Wrong number of arguments!")
        print("Correct usage of populator:")
        print(f"py populator.py <preset.json> [amount={DEFAULT_POPULATION_COUNT}]")
        return

    count = int(sys.argv[2]) if len(sys.argv) == 3 else DEFAULT_POPULATION_COUNT

    print(f"Searching for {sys.argv[1]} in {PRESET_DIR}")
    candidates = find_candidates(sys.argv[1])
    
    if len(candidates) == 0:
        print("No such preset found!")
        return
    if len(candidates) == 1:
        populate(candidates[0], count)
        return
    
    print("Multiple candidates found; choose which one you want:")
    for i in range(len(candidates)):
        print(f"{i + 1}. {candidates[i].name}")
    
    which = int(input("Enter number: "))

    if not 1 <= which <= len(candidates):
        print("Invalid number!")
        return

    populate(candidates[which - 1], count)


if __name__ == "__main__":
    main()