# Dynasty of Rot — каталог сборки

Сборка: **Dynasty of Rot**  
Игра: **Minecraft 1.21.1**  
Ядро сервера: **Youer `1.21.1-48ee2281`** (успешный локальный compile, `C:\Foliaaa\dist\youer-pmt-1.21.1`, 2026-08-03)  
Загрузчик модов: **NeoForge 21.1.248**  
Гибрид: моды NeoForge + API Paper/Purpur/Bukkit на одном процессе.

Что это за пак по смыслу: **цивилизация + двор + RPG-классы + вампиризм + кухня**. Не тех-пак и не «всё подряд». Название сервера к геймдизайну не прибито — это вывеска.

Источник правды по файлам: `server/mods` (сервер), `server/mods-client` (клиент), `server/mods-disabled` (снято), `server/mods-added` (добавлено в этом проходе).

---

## Почему старое ядро не жило

Лог `mods/latest.log` (21.1.241):

1. **Cubes Without Borders** грузил клиентский класс на dedicated server → FATAL, сервер не вставал.
2. **NeoForge 21.1.241** слишком стар для модов вроде XercaPaint (`21.1.242+`). Новое ядро **21.1.248** это закрывает.
3. **Farmers Delight Christmas Edition** — сборка **1.20.1** в папке 1.21.1. Праздник отложен в `mods-disabled`.
4. **C2ME** снят: у PMT-Youer свои потоки сущностей. Параллельная генерация чанков с гибридным chunk map уже кусалась (Sable/Youer). Не включаем `parallel-vanilla-worlds`.
5. **Responsive Shields 2.4** — mixin в tick-const щита на Youer даёт `InjectionError`. Снят в `mods-disabled`.
6. **Apothic Enchanting** — обязателен для Apotheosis 8.x; без него Supplementaries/Amendments падают на `EnchantmentStatBlock`. Добавлен `1.21.1-1.6.1`.

`youer.auto_delete_mods` выключен. Ядро не имеет права само выкидывать банки.

---

## Карта сборки

| Слой | За что отвечает |
|---|---|
| Цивилизация | MineColonies, Millénaire, Small Colonies, TownTalk, MC Trade Post, Structurize, Domum Ornamentum, BlockUI |
| Двор / политика | Easy Factions, War & Taxes, Warn Nobility, FTB Teams, SDM Economy / Shop |
| RPG-классы | Spell Engine, Spell Power, Archers, Paladins, Rogues, Wizards, Artificers, Arsenal, More RPG Library, RPG Minibosses |
| Прокачка лута | Apotheosis + Apothic* , Champions, Relics, Runes, Skill Tree, Pufferfish Skills |
| Ночь | Vampirism + Bloodlines, Ageing, Umbrellas, Covered Armor, Integrations |
| Кухня | Farmer's Delight и аддоны |
| Компаньоны | DoggyTalents, Dragon Mounts, Horseman, MCA |
| Клиентский рендер | Sodium-стек, куллинг, миникарта — только `mods-client` |

---

## Полный разбор модов, что уже были в папке

### Библиотеки и клей

| Файл / мод | Роль | Сторона |
|---|---|---|
| architectury | API для многих модов | обе |
| cloth-config | GUI конфигов | обе |
| geckolib / azurelib / azurelibarmor / lionfishapi | Анимации сущностей/брони | обе |
| kotlinforforge | Kotlin на NeoForge | обе |
| moonlight | Библиотека Moonlight (Supplementaries и кухня) | обе |
| Placebo | Библиотека Shadows (Apotheosis) | обе |
| PuzzlesLib | Библиотека Puzzles (Universal Bone Meal и др.) | обе |
| curios + accessories | Слоты аксессуаров. **Два стека сразу** — часть модов на Curios, часть на Accessories. Не выкидывать «дубль» без проверки зависимостей | обе |
| player-animation-lib-forge + PlayerAnimationLibNeoforge | Два разных player-animator. Spell Engine / Better Combat и часть портов тянут разные артефакты | обе |
| owo-lib | UI/сеть библиотека | обе |
| txnilib | Библиотека Txni | обе |
| jamlib | Библиотека Jam | обе |
| cupboard | Мелкий хелпер (часто AllTheLeaks/куллинг) | обе |
| cyclopscore | Ядро Cyclops (EvilCraft) | обе |
| Guide-API-VP / modonomicon | Книги гайдов | обе |
| structure_pool_api | Пулы структур | обе |
| ranged_weapon_api | API дальнего оружия для Spell-линейки | обе |
| more_rpg_library | Общая RPG-база классов | обе |
| extra spell attributes | Доп. атрибуты заклинаний | обе |
| ftb-library / ftb-teams | Команды и UI FTB | обе |
| forgified-fabric-api + connector | Sinytra: Fabric API на NeoForge. Нужен, пока в паке есть порты | обе |
| oracle_index | Индекс/поиск контента модов | обе |

### RPG, бой, заклинания

| Мод | Что делает |
|---|---|
| spell_engine / spell_power / runes | Ядро заклинаний, сила заклинаний, руны |
| archers / archers_expansion | Класс лучника и расширение |
| paladins | Священник/паладин |
| rogues | Плут |
| wizards | Маг |
| artificers | Инженер-маг |
| arsenal | Оружейный контент под Spell Engine |
| gazebo | Контент/структуры под ту же линейку |
| rpg-minibosses | Мини-боссы под RPG |
| village_taverns | Таверны как точка RPG-контента |
| bettercombat | Анимации ближнего боя вместо ванильной палки |
| apothiccombat | Стык Apotheosis и Better Combat |
| critical_strike | Крит-система |
| bowinfinityfix | Бесконечность на луке работает как все думают, что она работает |
| ~~responsiveshields~~ | Снят: mixin `setShieldUseDelay` не цепляется в Youer (нет refMap, 0/1 inject). В `mods-disabled` |
| ParCool | Паркур. Нагрузка на клиент и на сущности — следить |
| skill_tree | Дерево навыков |
| puffish_skills | Ещё одно дерево навыков. **Два дерева в одном паке** — игроку объяснить, какое за что, иначе каша |
| relics | Реликвии в слотах |
| Champions | Чемпионские мобы с аффиксами |
| Apotheosis | Ветвь приключений: спавнеры, лут, деревня, зачары |
| ApothicAttributes / ApothicSpawners / apothic_compats | Атрибуты, спавнеры, совместимости Apotheosis |

### Цивилизация и двор

| Мод | Что делает |
|---|---|
| minecolonies | Большие колонии с профессиями и стройкой |
| structurize / blockui / domum-ornamentum | Строительные GUI и декорации колоний |
| smallcolonies | Компактные колонии, меньше микроменеджмента |
| millenaire | Культурные деревни NPC (бета 9.0) |
| towntalk | Озвучка/диалоги колоний |
| mctradepost | Торговые посты |
| easy_factions | Фракции игроков |
| WarNTaxes | Война и налоги |
| warnnobility | Дворянство / титулы |
| sdmeconomy / sdmshopa | Экономика и магазин |
| royal-variations | Вариации «королевского» контента |

### Вампиризм

| Мод | Что делает |
|---|---|
| Vampirism | Играбельные вампиры, охотники, фракции ночи |
| bloodlines | Родословные |
| vampiricageing | Возраст и сила со временем |
| VampiresNeedUmbrellas | Зонт как инструмент выживания днём |
| vampirismcoveredarmor | Закрытая броня vs солнце |
| vampirism_integrations | Стыки с другими модами |

### Кухня и ферма

| Мод | Что делает |
|---|---|
| FarmersDelight | Базовая кухня, ножи, плиты, грядки |
| extradelight / expandeddelight / moredelight / casualnessdelight / cuisinedelight | Расширения рецептов и утвари |
| ends_delight / EggDelight / SeedDelight / pineapple_delight / berries_and_cherries | Тематические продукты |
| AutochefsDelight | Автокухня |
| delightlib | Общая библиотека части Delight-модов |
| rightclickharvest | Сбор урожая ПКМ |
| smarterfarmers | Фермеры ведут себя менее как декорации |
| UniversalBoneMeal | Костная мука на больше вещей |
| fastleafdecay | Листва не висит до следующего патча |
| wooltostring | Шерсть → нить, логистика колоний |
| yafda | Ещё один food/farm аддон |
| polymorph | Конфликт рецептов: выбор крафта |

**Снято:** `farmers_delight_christmas_edition` 1.20.1.

### Компаньоны, существа, мир

| Мод | Что делает |
|---|---|
| DoggyTalentsNext | Собаки с талантами |
| Dragon Mounts Remastered | Драконы как маунты |
| horseman | Лошадиный контент |
| TameableZombieSkeletonHorse | Приручение нежити-лошадей |
| saddle_craft | Крафт сёдел |
| mca | Minecraft Comes Alive: жители как персонажи |
| geneticsresequenced | Генетика / сиринги |
| theurgy | Алхимия |
| evilcraft | Тёмная магия Cyclops |
| aaron | Контентный мод из папки (сборка aaron) |

### Карты, QoL, интерфейс

| Мод | Что делает | Сторона |
|---|---|---|
| Jade | Подсказки по блокам | клиент |
| JEI / JEED / JustEnoughResources | Рецепты, эффекты, дроп | обе |
| catalogue | Список модов в меню | клиент |
| xaero minimap | Миникарта | клиент |
| BetterThirdPerson | Камера от третьего лица | клиент |
| xercapaint | Картины / холсты. Хочет NeoForge **21.1.242+** — на 248 ок | обе |
| playercollars | Косметика/аксессуар | обе |
| easydisenchanting | Снятие чар без лотереи | обе |
| foolproof | Защита от дурацких кликов по важным блокам | обе |
| underlay | Слои блоков / декоративный QoL | обе |

### Производительность и стабильность (сервер)

| Мод | Что делает | Статус |
|---|---|---|
| lithium | Общая оптимизация тика | сервер (есть и клиент, но в паке оставлен на сервере) |
| modernfix | Ускорение загрузки и дыры модлоадера | обе |
| ferritecore | Меньше RAM на блокстейтах | обе |
| alltheleaks | Закрытие утечек модов 1.21 | обе |
| fastasyncworldsave | Асинхронное сохранение мира | сервер |
| Lagshield / memguard | Щиты по памяти/лагу. Слой пересекается с AllTheLeaks — не плодить ещё | сервер |
| evolution + cupboard | Трейты мобов (Someaddon). `side=BOTH`, в server-only кит не класть | обе |
| eh2.0 Enhanced Hordes | Орды. `side=BOTH` | обе |
| Chunky / ChunkyMcChunkFace | Предген и удержание чанков | сервер |
| c2me | Многопоточные чанки | **disabled** на этом ядре |
| Connector | Fabric-на-NeoForge | обе, пока нужен FAPI |

### Производительность (клиент, вынесено)

Sodium, Sodium Extra, Reese's Sodium Options, Sodium Dynamic Lights, ImmediatelyFast, EntityCulling, MoreCulling, BadOptimizations, Ixeris, GPU mem leak fix, Cubes Without Borders, Puzzle, Modpack Update Checker.

CWB на dedicated — причина падения 22.08.2026.

---

## Что добавлено (рекомендация сборщика)

Скачивается в `mods-added` и раскладывается в сервер/клиент. Если Modrinth не отдал файл — строка всё равно остаётся в плане, банк докладывается вручную.

| Мод | Зачем именно этой сборке |
|---|---|
| **Simple Voice Chat** | Колонии и осады без войса — это чат-опера. База для SMP. |
| **Balm + Waystones** | Телепорты между колониями. Иначе карта — наказание. |
| **Open Parties and Claims** | FTB Chunks на Modrinth для 1.21.1 NeoForge нет. OPAC закрывает чанки и пати. FTB Teams остаются для команд. |
| **Combat Roll** | Пара к Better Combat. Бой перестаёт быть танцем на месте. |
| **Comforts** | Спальники/гамаки. Вампирам и тем, кто не скипает ночь колонии. |
| **Corpse** | Труп на месте смерти. MineColonies + RPG + вампиры = частые смерти в чужих чанках. |
| **Supplementaries + Amendments** | Быт колоний: знаки, гонг, детали. Amendments чинит ванильные дыры под Moonlight. |
| **Carry On** | Перенос мебели при переезде колонии. |
| **Sophisticated Core + Backpacks** | Инвентарь под данжи и стройку. |
| **YUNG's API + Better Nether / End Island / Bridges** | Структуры, чтобы мир не был только «колония посреди плоского поля». |
| **Packet Fixer + Connectivity** | Тяжёлый модлист рвёт сеть. Это не «у тебя Wi‑Fi». |
| **Spark** | Профилирование TPS. Без него оптимизация — фольклор. |
| **Almost Unified** | Куча Delight-рецептов. Унификация руды/еды, меньше дублей в JEI. |
| **Nature's Compass + Explorer's Compass** | Поиск биомов и структур на большой предген-карте. |
| **Xaero's World Map** | Пара к уже стоящей миникарте. |
| **Noisium / ServerCore** | Серверный тик без C2ME. |
| **Crash Assistant** | Клиентский разбор краша без «скинь latest.log в 40 сообщений». |
| **Dynamic FPS / Mouse Tweaks / Controlling / Searchables / Trade Cycling** | Клиентский QoL: АФК не жрёт GPU, сортировка инвентаря, поиск биндов, цикл трейдов. |
| **Kuma API / Resourceful Lib / Bookshelf** | Часто требуются как зависимости указанных модов. |
| **Puzzles Lib** | Уже мог быть; дубль не кладём. |
| **Apothic Enchanting 1.21.1-1.6.1** | Обязательный модуль Apotheosis 8.x. Без него Supplementaries/Amendments падают на `EnchantmentStatBlock`. |
| **Moonlight 3.5.0** | Поднят с 3.3.2: Supplementaries 3.9.1 зовёт `ILightable.isIgniter`, старая банка этого метода не имела. |

Не добавлено сознательно:

- **Create** — тяжёлый тех-стек, колонии и Create дерутся за CPU.
- **Alex's Mobs / Ice and Fire** — зоопарк поверх RPG-боссов и драконов-маунтов.
- **Distant Horizons** — красиво, по RAM не для этой плотности модов.
- **Второй вампирский оверхол** (Werewolves и т.п.) — сначала стабилизировать Vampirism.
- **KubeJS** — пока нет скриптов баланса, только лишняя поверхность поломок.

---

## Как гонять стабильно

1. Сервер: `server\start.bat` (Windows) или `start.sh` (Linux). Ядро: `youer.jar` = `youer-1.21.1-48ee2281-server.jar`.
2. JVM: `user_jvm_args.txt`. На 32 ГБ хосте поднимайте `-Xmx` до 18G как на старом NoteBuns-старте. На слабее — 8–12G.
3. `youer_experemental.parallel-entities: true`, `parallel-vanilla-worlds: false`.
4. Клиент только через `Launcher`. Смешивать Sodium в `server/mods` нельзя.
5. Первый запуск Youer качает libraries. Это норма, не краш.
6. После бута: `spark profiler start` на 60c, смотреть entity tick колоний и спавнеров Apotheosis.

---

## Известные трения внутри пака

- **Curios + Accessories** — два слотовых API.
- **skill_tree + puffish_skills** — два дерева скиллов.
- **MineColonies snapshot + Millénaire beta + Small Colonies** — три градостроителя. Игрокам сказать, какой канон сервера, иначе три ратуши на чанк.
- **Connector** на dedicated иногда орёт на brigadier. Если после выноса клиента сервер жив — оставить. Если нет — вынести Connector+FAPI только в клиент и убрать чисто Fabric-порты.
- **ParCool + Better Combat + Spell Engine** — анимации могут конфликтовать. Если T-позы: выключить ParCool первым.
- **Responsive Shields** — снят с dedicated: Youer ломает inject в константу задержки щита. Better Combat остаётся.
- **oracle_index** (~30 МБ) и **towntalk** (~50 МБ) и **minecolonies** (~78 МБ) — тяжёлые банки, это не «ошибка копирования».
