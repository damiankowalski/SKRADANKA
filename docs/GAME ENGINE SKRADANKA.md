# AI Game Engine

We're building a production-grade, simple and straight-forward 3D Game Engine for Windows. This is professional-grade software that needs to be stable, fully-tested, and real-world deployable. No hacks, no shortcuts - solid as a rock.

## Dev Workflow
- Ask questions only when absolutely necessary - work autonomously
- IMRORTANT! DO NOT look outside of working directory or use other code on my computer as reference
- Start by asking question to determine engine name. Suggest a few
- Testing is of the utmost importance - use lots of unit tests (any framework, simple is good), run automated testing
- Setup a scene used for testing, which tests every single feature in the engine - all components, and entire scripting API
- Do things properly, this is production-grade and not a hack project
- Create AGENTS.md and relevant skills to support development, and contain concrete development guidelines
- Code style to match [Hazel](https://docs.hazelengine.com/HazelForEngineers/DeveloperGuide#naming), full repo [here](https://github.com/TheCherno/Hazel)
- Create git repository, commit and push to [GitHub repo](https://github.com/damiankowalski/SKRADANKA). IMPORTANT: do a code review before committing, and make sure all changes comply with code style, production-grade quality standards, and have been properly tested/have unit tests that have passed where necessary

## Tech stack
- C++ and CMake for core engine
- [GLFW](https://github.com/glfw/glfw) and [nvrhi](https://github.com/NVIDIA-RTX/NVRHI)
- [glm](https://github.com/g-truc/glm) for math
- [miniaudio](https://github.com/mackron/miniaudio) for audio
- Lua for scripting, though this can be up for discussion

## Basic Architecture
- Static library for core engine, executable for editor and runtime (depending on chosen design)
- Need to have editor to build games - this can be embedded into runtime executable (and stripped from distribution builds), or can be a standalone executable
- Simple ECS perhaps, to author scene with entities and components
- Ability to "export" game - executable that runs game without editing ability that we can distribute
- Editor needs to be fully controllable by AI agents - I should be able to ask you to build me a game like Tetris, and you should have all the tools available to do so without my intervention

## 3D Renderer
- Import gltf meshes with materials and textures
- Editor has gizmo to position them in the world
- PBR material workflow
- IBL with HDRIs from https://polyhaven.com/hdris
- Soft shadow maps
- Good SSAO
- HDR pipeline with tonemapping
- as this engine is mainly to create stealth 3d TPP game, cool if this can work with RTX
- base on thief the dark project mechanisms where STEM technology was used - I hope I remember this right - meaning water arror can interact with torch, fire arrow can make powder keg explode

## 3D Physics
- Use simple 3D physics library of your choosing
- Controllable via scripting, authored via components in editor

## Behavior
- Scripting with Lua (or other chosen language)
- Control entities/components and run in update loop, and entity destruction/creation
- Spawn new entities/prefabs

## AI (in polish) summary this engine should be able to control and sript AI in this game. Take is as example only as this if taken from old draft, but general rule of thumb is that AI awarness must be workign based on audio, player possition in shadows + AI must cast ray and verify if player can be seen based on background AI can 'see' the player

# ShadowProto — percepcja NPC oparta na kontraście sylwetki

## 1. Problem

Klasyczne skradanki odpowiadają na pytanie „czy gracz stoi w cieniu?”. To pytanie o **oświetlenie gracza**, a nie o to, **co widzi obserwator**. Ludzkie oko nie mierzy bezwzględnej jasności obiektu, tylko **kontrast** między obiektem a tym, co jest za nim.

Skutek w prawdziwym świecie: człowiek stojący w całkowitej ciemności, ale na tle jasnego okna, wejścia do oświetlonej sali czy zachodzącego słońca, jest doskonale widoczny jako czarna sylwetka. W większości gier jest wtedy niewidzialny.

## 2. Główna idea

NPC nie pyta „jak jasno jest u gracza?”, tylko:

> **„Czy w miejscu, w które patrzę, coś odcina się od tła wystarczająco mocno, żeby moje oko to zarejestrowało?”**

Dla każdego punktu sylwetki gracza widocznego dla NPC porównujemy dwie wartości wzdłuż tej samej linii wzroku:

- **L_p** — jasność gracza (ile światła odbija w stronę NPC),
- **L_b** — jasność tła, czyli powierzchni, w którą trafiłby wzrok NPC, gdyby gracza tam nie było (promień „przebija” gracza i leci dalej).

Wykrywalność zależy od różnicy tych dwóch wartości, a nie od samego L_p.

## 3. Macierz sytuacji

| Gracz | Tło za graczem (z perspektywy NPC) | Kontrast | Efekt |
|---|---|---|---|
| ciemny | ciemne | niski | **niewidoczny** — klasyczny cień |
| ciemny | jasne | wysoki (sylwetka) | **widoczny** — nowa mechanika |
| jasny | ciemne | wysoki | widoczny — klasyka |
| jasny | jasne | niski | **trudny do zauważenia** — „kamuflaż w świetle” |

Ostatni wiersz jest ciekawym efektem ubocznym: gracz może być względnie bezpieczny w pełnym świetle, jeśli stoi na tle równie jasnej ściany. To odwraca intuicję gatunku i daje nowe decyzje taktyczne.

## 4. Model percepcji krok po kroku

### 4.1 Próbkowanie sylwetki

Gracz nie jest punktem. Wybieramy kilka punktów kontrolnych na jego obrysie (np. głowa, ramiona, boki, środek; 5–9 punktów).

Dla każdego punktu `i`:

1. Promień z oka NPC do punktu `i`. Jeśli coś go zasłania → punkt niewidoczny.
2. Jeśli widoczny: odczytaj `L_p[i]` (jasność gracza w tym punkcie).
3. Przedłuż promień za gracza do pierwszej przeszkody → odczytaj `L_b[i]` (jasność tła).

Wynik:

```
f = liczba_widocznych_punktów / liczba_wszystkich_punktów     // ułamek widocznej sylwetki
```

Dzięki temu gracz wychylający tylko głowę zza skrzyni na tle okna jest częściowo wykrywalny, a nie „wszystko albo nic”.

### 4.2 Kontrast

Używamy kontrastu względnego (w stylu Webera), bo oko reaguje na proporcje, nie na różnice bezwzględne:

```
C[i] = |L_b[i] - L_p[i]| / max(L_b[i], L_p[i], ε)
C_eff = średnia z C[i] dla widocznych punktów      // 0 = brak kontrastu, 1 = maksymalny
```

`ε` to mała stała chroniąca przed dzieleniem przez zero w całkowitej ciemności. Jeśli zarówno gracz, jak i tło są czarne, kontrast wynosi praktycznie zero → niewidoczny.

### 4.3 Adaptacja oka NPC

Oko nie ma stałej czułości. NPC ma wewnętrzny stan **L_a** — poziom jasności, do którego jego wzrok jest przyzwyczajony (średnia jasność tego, na co ostatnio patrzył / gdzie stoi).

- W jasności oko gorzej rozróżnia subtelne różnice w ciemnych partiach.
- W ciemności rośnie czułość na słabe światło, ale potrzeba większego kontrastu, żeby wyłowić detal.
- **Adaptacja trwa:** do jasności szybko (sekundy), do ciemności wolno (dziesiątki sekund, skrócone dla rozgrywki).

```
L_a += (L_otoczenia - L_a) * (1 - exp(-dt / τ))
τ = τ_jasno  jeśli L_otoczenia > L_a   (szybko, np. 0.5 s)
τ = τ_ciemno jeśli L_otoczenia < L_a   (wolno,  np. 5–10 s)
```

Z adaptacji wynika **próg kontrastu**, którego NPC potrzebuje, żeby cokolwiek zauważyć:

```
C_t = C_min + k_dark / (L_a + ε)
```

Konsekwencje rozgrywkowe:

- strażnik, który właśnie wyszedł z jasnej sali na ciemny korytarz, jest przez chwilę „ślepy” na ciemne partie,
- strażnik z latarką podnosi sobie L_a i gorzej widzi to, co jest poza snopem,
- strażnik stojący długo w ciemności staje się wyczulony — sylwetka na tle nawet umiarkowanie jasnego tła zostanie złapana szybciej.

### 4.4 Odległość i atmosfera

Ważna obserwacja fizyczna: sama jasność powierzchni nie spada z odległością. To, co spada, to:

- **rozmiar kątowy** celu (mniejszy cel wymaga większego kontrastu),
- **kontrast** przez mgłę, dym, kurz (atmosfera „rozjaśnia” ciemne i przyciemnia jasne).

```
S = min(1, rozmiar_kątowy / rozmiar_referencyjny)       // współczynnik rozmiaru
C_eff = C_eff * exp(-β * odległość)                       // β = gęstość atmosfery, 0 w czystym powietrzu
```

Dym czy mgła stają się więc realnym narzędziem gracza, bo spłaszczają sylwetkę.

### 4.5 Pole widzenia i ruch

Stożek widzenia nie jest jednorodny:

- **centrum** — dobra rozdzielczość, wychwytuje nieruchomą sylwetkę,
- **peryferia** — słabe w detalu, ale bardzo czułe na **ruch**.

```
P(θ) = funkcja spadająca od 1 (środek stożka) do ~0.2 (krawędź)
M    = 1 + m_ruch * prędkość_gracza * (1 + peryferyjność)   // ruch liczy się mocniej na krawędzi
```

Stojąca nieruchomo sylwetka na skraju pola widzenia może przejść niezauważona; ta sama sylwetka w biegu — nie.

### 4.6 Bodziec i licznik wykrycia

Łączymy wszystko w jeden bodziec:

```
B = (C_eff / C_t) * f * S * P(θ) * M
```

- `B < 1` → poniżej progu percepcji, NPC nic nie rejestruje.
- `B ≥ 1` → NPC coś widzi; im większe B, tym szybciej.

Zamiast losowego rzutu kością w każdej klatce używamy **licznika podejrzliwości** D (0–1), który narasta w czasie:

```
jeśli B ≥ 1:  D += r_wzrost * (B - 1) * dt
w przeciwnym razie: D -= r_spadek * dt
D = clamp(D, 0, 1)
```

Progi licznika wyznaczają stan NPC:

| D | Stan | Zachowanie |
|---|---|---|
| 0 – 0.3 | nieświadomy | patrol |
| 0.3 – 0.7 | podejrzliwy | zatrzymuje się, obraca w stronę bodźca |
| 0.7 – 1.0 | szuka | idzie sprawdzić ostatnie miejsce bodźca |
| 1.0 | alarm | gracz wykryty |

To daje graczowi czytelny feedback i szansę reakcji, a jednocześnie wyklucza „niesprawiedliwe” natychmiastowe wykrycie przez pojedynczy piksel kontrastu.

## 5. Skąd brać jasność (L_p, L_b)

Niezależnie od silnika są trzy główne podejścia, od najtańszego do najwierniejszego:

1. **Analityczne** — suma wkładów pobliskich źródeł światła w danym punkcie (odległość, zasięg, zasłonięcie). Tanie, przewidywalne, łatwe do debugowania. Dobre na prototyp.
2. **Sondy / mapa jasności** — wcześniej wypieczona lub okresowo aktualizowana siatka wartości jasności poziomu. Szybki odczyt, słabo radzi sobie z dynamicznym światłem.
3. **„Oko NPC”** — renderowanie z perspektywy NPC małego obrazu (np. 64×32 px) wraz z maską gracza. Kontrast liczony bezpośrednio z pikseli: piksele gracza vs piksele otaczające jego obrys. Najbliższe temu, jak działa prawdziwe oko — automatycznie uwzględnia wszystko, co widać na ekranie. Najdroższe, więc tylko dla NPC, którzy mają gracza w stożku, i z niższą częstotliwością (np. 5–10 razy na sekundę).

Rekomendacja: zacząć od (1), zaprojektować interfejs tak, by dało się później podmienić na (3).

## 6. Konsekwencje dla gracza i projektowania poziomów

- Gracz musi myśleć **z perspektywy strażnika**, nie tylko o własnym oświetleniu: „co jest za mną, patrząc z jego miejsca?”.
- Okna, drzwi do oświetlonych pomieszczeń, ogniska, monitory, horyzont stają się elementami taktycznymi.
- Gaszenie światła działa w dwie strony: chowa gracza, ale też usuwa jasne tło, które mogło go zdradzić — albo zmienia adaptację strażnika.
- Wskaźnik widoczności w UI powinien pokazywać nie tylko „jestem w cieniu”, ale też **ryzyko sylwetki** względem najbliższego NPC.
- NPC mogą zyskać zachowania wykorzystujące mechanikę: celowo patrzą w stronę jasnych przejść, ustawiają się tyłem do światła, kierują latarkę na ściany zamiast na podłogę.

## 7. Parametry do strojenia

| Parametr | Znaczenie | Punkt startowy |
|---|---|---|
| liczba punktów sylwetki | dokładność vs koszt | 5–9 |
| `C_min` | minimalny kontrast wykrywalny w dobrych warunkach | 0.1 |
| `k_dark` | jak bardzo ciemność podnosi próg | do strojenia |
| `τ_jasno`, `τ_ciemno` | szybkość adaptacji | 0.5 s / 5–10 s |
| `β` | gęstość atmosfery | 0 (czysto) |
| `m_ruch` | wpływ ruchu | do strojenia |
| `r_wzrost`, `r_spadek` | tempo licznika podejrzliwości | do strojenia |
| progi stanów | 0.3 / 0.7 / 1.0 | jak wyżej |

## 8. Scenariusze testowe

1. Gracz w ciemnym kącie, za nim ciemna ściana → nie powinien być wykrywany.
2. Ten sam kąt, ale między graczem a ścianą otwarte drzwi do jasnej sali → wykrycie w ciągu kilku sekund.
3. Gracz w pełnym świetle na tle jasnej ściany → wykrycie wolniejsze niż na tle ciemnym.
4. Strażnik wchodzi z jasnej sali do ciemnego korytarza → przez kilka sekund nie zauważa gracza, potem „przejrzy”.
5. Gracz wychyla tylko głowę zza osłony na tle okna → wykrycie częściowe, wolniejsze niż pełnej sylwetki.
6. Gracz na skraju stożka: nieruchomy vs biegnący → biegnący wykryty, nieruchomy nie.
7. Ta sama scena z dymem → sylwetka przestaje być czytelna z dystansu.

