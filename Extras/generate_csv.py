import os
import csv
import re

def parse_toml_file(filepath):
    items = []
    current_item = None
    
    if not os.path.exists(filepath):
        print(f"File not found: {filepath}")
        return []
        
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            # Header check
            if line.startswith('[[') and line.endswith(']]'):
                if current_item:
                    items.append(current_item)
                current_item = {}
                continue
                
            # Key-value pair check
            if '=' in line:
                parts = line.split('=', 1)
                k = parts[0].strip()
                v = parts[1].strip()
                
                # Strip quotes or handle array
                if v.startswith('"') and v.endswith('"'):
                    v = v[1:-1]
                elif v.startswith('[') and v.endswith(']'):
                    arr_content = v[1:-1].strip()
                    if arr_content:
                        v = [val.strip().strip('"') for val in arr_content.split(',')]
                    else:
                        v = []
                else:
                    try:
                        v = int(v)
                    except ValueError:
                        if v.lower() == 'true':
                            v = True
                        elif v.lower() == 'false':
                            v = False
                
                if current_item is not None:
                    current_item[k] = v
                    
        if current_item:
            items.append(current_item)
            
    return items

def main():
    # Dados de itens vivem em Docs/Data (TOMLs fonte + CSV gerado)
    docs_dir = r"d:\JOGOS\PROJETO LEGAIA\CODE\JRPGFramework\Docs\Data"
    itens_data_dir = os.path.join(docs_dir, "ITENS DATA")
    
    # Load files
    raw_items = parse_toml_file(os.path.join(itens_data_dir, "items.toml"))
    raw_armor = parse_toml_file(os.path.join(itens_data_dir, "armor.toml"))
    raw_accessories = parse_toml_file(os.path.join(itens_data_dir, "accessories.toml"))
    raw_weapons = parse_toml_file(os.path.join(itens_data_dir, "weapons.toml"))
    
    csv_rows = []
    
    # 1. Process items.toml
    # Categories: consumable, permanent_stat, key, art_book, fishing_lure
    # As Waters nao trazem effect_class no TOML — so a frase em ingles. Sem este
    # mapa elas saem com EffectClass vazio e EffectValue 0, e o
    # UStatusSubsystem::ApplyPermanentStatUpgrade nao faz nada. Os numeros sao os
    # da propria descricao de cada uma.
    waters_map = {
        "life_water":     ("hp_max", 16),
        "magic_water":    ("mp_max", 8),
        "power_water":    ("attack", 4),
        "guardian_water": ("defense", 4),   # sobe UDF e LDF
        "swift_water":    ("speed", 4),
        "wisdom_water":   ("intelligence", 4),
        "miracle_water":  ("all_stats", 4), # os cinco de combate; AGL fica de fora
        "honey":          ("all_stats", 4),
    }

    art_books_map = {
        "fire_book_i": ("tornado_flame", "Vahn"),
        "fire_book_ii": ("fire_blow", "Vahn"),
        "fire_book_iii": ("burning_flare", "Vahn"),
        "wind_book_i": ("frost_breath", "Noa"),
        "wind_book_ii": ("vulture_blade", "Noa"),
        "wind_book_iii": ("hurricane_kick", "Noa"),
        "thunder_book_i": ("thunder_punch", "Gala"),
        "thunder_book_ii": ("lightning_storm", "Gala"),
        "thunder_book_iii": ("explosive_fist", "Gala")
    }
    
    for ri in raw_items:
        key = ri.get("key", "")
        name = ri.get("name", "")
        cat_raw = ri.get("category", "")
        price = ri.get("price", 0)
        effect = ri.get("effect", "")
        
        # Map Category
        category = "Consumable"
        if cat_raw == "permanent_stat":
            category = "PermanentStat"
        elif cat_raw == "key":
            category = "Key"
        elif cat_raw == "art_book":
            category = "ArtBook"
        elif cat_raw == "fishing_lure":
            category = "FishingLure"
            
        # Map UseContext
        use_context = "AnyTime"
        if category == "PermanentStat" or category == "ArtBook":
            use_context = "FieldOnly"
        elif category == "Key":
            if "rod" in key:
                use_context = "FieldOnly" # varas de pesca
            else:
                use_context = "QuestOnly"
        elif category == "FishingLure":
            use_context = "FieldOnly"
        else: # Consumable
            if "elixir" in key or "boost" in key:
                use_context = "BattleOnly"
            elif "door" in key or "incense" in key:
                use_context = "FieldOnly"
                
        # Parse effects
        heal_hp = 0
        heal_mp = 0
        b_target_all = "False"
        b_revive = "False"
        b_cure_status = "False"
        b_heal_ap = "False"
        
        effect_lower = effect.lower()
        # "Permanently raises maximum HP by 16" nao e cura: a heuristica abaixo
        # procura a palavra "maximum" e concluia cura total (9999) nas Waters.
        # PermanentStat sobe atributo e ponto — quem cura e Consumable.
        if category == "PermanentStat":
            effect_lower = ""
        if "hp" in effect_lower:
            if "200" in effect_lower:
                heal_hp = 200
            elif "800" in effect_lower:
                heal_hp = 800
            elif "maximum" in effect_lower:
                heal_hp = 9999
        if "mp" in effect_lower:
            if "50" in effect_lower:
                heal_mp = 50
            elif "200" in effect_lower:
                heal_mp = 200
            elif "maximum" in effect_lower:
                heal_mp = 9999
        if "each character" in effect_lower or "to each character" in effect_lower:
            b_target_all = "True"
        if "phoenix" in effect_lower or "fallen ally" in effect_lower or "revive" in effect_lower:
            b_revive = "True"
        if "cures the poison" in effect_lower or "cures all status" in effect_lower or "cures" in effect_lower:
            b_cure_status = "True"
        if "attack gauge" in effect_lower:
            b_heal_ap = "True"
            
        teaches_art = ""
        teaches_char = ""
        if category == "ArtBook" and key in art_books_map:
            teaches_art, teaches_char = art_books_map[key]
            
        row = {
            "---": key,
            "Key": key,
            "Name": name,
            "Category": category,
            "BuyPrice": price,
            "EffectDescription": effect,
            "UseContext": use_context,
            "Icon": "",
            "HealHP": heal_hp,
            "HealMP": heal_mp,
            "bHealAP": b_heal_ap,
            "bCureStatus": b_cure_status,
            "bRevive": b_revive,
            "bTargetAll": b_target_all,
            "TeachesArt": teaches_art,
            "TeachesCharacter": teaches_char,
            "WeaponType": "",
            "EquipBest": "",
            "EquipOthers": "",
            "AttackBonus": 0,
            "ArmorSlot": "None",
            "EquipCharacter": "",
            "UDF": 0,
            "LDF": 0,
            "EffectClass": waters_map.get(key, ("", 0))[0],
            "EffectValue": waters_map.get(key, ("", 0))[1],
            "StatusType": "",
            "ElementType": "",
            "Summons": ""
        }
        csv_rows.append(row)
        
    # 2. Process armor.toml
    for ra in raw_armor:
        key = ra.get("key", "")
        name = ra.get("name", "")
        price = ra.get("price", 0)
        slot_raw = ra.get("slot", "")
        udf = ra.get("udf", 0)
        ldf = ra.get("ldf", 0)
        equip = ra.get("equip", "")
        
        armor_slot = "None"
        if slot_raw == "armor":
            armor_slot = "Armor"
        elif slot_raw == "helmet":
            armor_slot = "Helmet"
        elif slot_raw == "shoes":
            armor_slot = "Shoes"
            
        row = {
            "---": key,
            "Key": key,
            "Name": name,
            "Category": "Armor",
            "BuyPrice": price,
            "EffectDescription": "",
            "UseContext": "Automatic",
            "Icon": "",
            "HealHP": 0,
            "HealMP": 0,
            "bHealAP": "False",
            "bCureStatus": "False",
            "bRevive": "False",
            "bTargetAll": "False",
            "TeachesArt": "",
            "TeachesCharacter": "",
            "WeaponType": "",
            "EquipBest": "",
            "EquipOthers": "",
            "AttackBonus": 0,
            "ArmorSlot": armor_slot,
            "EquipCharacter": equip,
            "UDF": udf,
            "LDF": ldf,
            "EffectClass": "",
            "EffectValue": 0,
            "StatusType": "",
            "ElementType": "",
            "Summons": ""
        }
        csv_rows.append(row)
        
    # 3. Process accessories.toml
    for rac in raw_accessories:
        key = rac.get("key", "")
        name = rac.get("name", "")
        price = rac.get("price", 0)
        effect = rac.get("effect", "")
        eff_class = rac.get("effect_class", "")
        eff_val = rac.get("effect_value", 0)
        status = rac.get("status", "")
        element = rac.get("element", "")
        summons = rac.get("summons", "")
        
        row = {
            "---": key,
            "Key": key,
            "Name": name,
            "Category": "Accessory",
            "BuyPrice": price,
            "EffectDescription": effect,
            "UseContext": "Automatic",
            "Icon": "",
            "HealHP": 0,
            "HealMP": 0,
            "bHealAP": "False",
            "bCureStatus": "False",
            "bRevive": "False",
            "bTargetAll": "False",
            "TeachesArt": "",
            "TeachesCharacter": "",
            "WeaponType": "",
            "EquipBest": "",
            "EquipOthers": "",
            "AttackBonus": 0,
            "ArmorSlot": "None",
            "EquipCharacter": "",
            "UDF": 0,
            "LDF": 0,
            "EffectClass": eff_class,
            "EffectValue": eff_val,
            "StatusType": status,
            "ElementType": element,
            "Summons": summons
        }
        csv_rows.append(row)
        
    # 4. Process weapons.toml
    for rw in raw_weapons:
        key = rw.get("key", "")
        name = rw.get("name", "")
        price = rw.get("price", 0)
        attack = rw.get("attack", 0)
        equip_best = rw.get("equip_best", "")
        equip_others = rw.get("equip_others", [])
        
        # Determine weapon type
        wpn_type = "Sword"
        key_l = key.lower()
        if "claw" in key_l or "nails" in key_l or "fangs" in key_l:
            wpn_type = "Claw"
        elif "axe" in key_l:
            wpn_type = "Axe"
        elif "mace" in key_l or "club" in key_l:
            wpn_type = "Club"
        elif "knife" in key_l or "glove" in key_l or "fist" in key_l:
            wpn_type = "Knife"
            
        # Format equip_others as UE array string, e.g. '("Vahn","Noa")'
        eq_others_str = ""
        if equip_others:
            eq_others_str = f'({",".join([f'"{x}"' for x in equip_others])})'
            
        row = {
            "---": key,
            "Key": key,
            "Name": name,
            "Category": "Weapon",
            "BuyPrice": price,
            "EffectDescription": "",
            "UseContext": "Automatic",
            "Icon": "",
            "HealHP": 0,
            "HealMP": 0,
            "bHealAP": "False",
            "bCureStatus": "False",
            "bRevive": "False",
            "bTargetAll": "False",
            "TeachesArt": "",
            "TeachesCharacter": "",
            "WeaponType": wpn_type,
            "EquipBest": equip_best,
            "EquipOthers": eq_others_str,
            "AttackBonus": attack,
            "ArmorSlot": "None",
            "EquipCharacter": "",
            "UDF": 0,
            "LDF": 0,
            "EffectClass": "",
            "EffectValue": 0,
            "StatusType": "",
            "ElementType": "",
            "Summons": ""
        }
        csv_rows.append(row)
        
    # Write to CSV
    csv_file_path = os.path.join(docs_dir, "DT_Items.csv")
    headers = [
        "---", "Key", "Name", "Category", "BuyPrice", "EffectDescription", "UseContext", "Icon",
        "HealHP", "HealMP", "bHealAP", "bCureStatus", "bRevive", "bTargetAll", "TeachesArt",
        "TeachesCharacter", "WeaponType", "EquipBest", "EquipOthers", "AttackBonus", "ArmorSlot",
        "EquipCharacter", "UDF", "LDF", "EffectClass", "EffectValue", "StatusType", "ElementType",
        "Summons"
    ]
    
    with open(csv_file_path, 'w', encoding='utf-8', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=headers)
        writer.writeheader()
        for r in csv_rows:
            writer.writerow(r)
            
    print(f"Generated {len(csv_rows)} rows and saved to {csv_file_path}")

if __name__ == "__main__":
    main()
