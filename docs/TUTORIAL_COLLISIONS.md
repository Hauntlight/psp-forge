# Tutorial: Bounding Boxes & Rilevamento Collisioni (2D & 3D) 💥

Questo tutorial spiega come gestire hitboxes, trigger ed ostacoli solidi in 2D e 3D su PlayStation Portable utilizzando le funzioni matematiche native di collisione di **PSP-Forge**.

La demo funzionante associata a questa guida si trova in:
`demos/demo_collisions/`

---

## 1. Tipi di Geometrie di Collisione Supportate

Per evitare calcoli pesanti di intersezione poligonale complessa (che saturerebbero la CPU MIPS a 333 MHz), la fisica nei giochi arcade e d'azione si affida a **volumi delimitatori (Bounding Volumes)** veloci da verificare in pochi cicli di clock:

| Geometria | Dimensione Memoria | Utilizzo Tipico |
|---|---|---|
| **`ForgeRect`** (2D AABB) | 16 byte ($x, y, w, h$) | Personaggi, piattaforme, ostacoli quadrati, zone trigger |
| **`ForgeCircle`** (2D Cerchio) | 12 byte ($x, y, r$) | Monete collezionabili, proiettili, sfere di energia |
| **`ForgeAABB`** (3D Box) | 24 byte ($\min_{xyz}, \max_{xyz}$) | Veicoli, corsie, muri e blocchi di livello 3D |
| **`ForgeSphere`** (3D Sfera) | 16 byte ($\text{centro}_{xyz}, r$) | Raggio di raccolta, proiettili 3D, sfere di culling |

---

## 2. L'API di Collisione in `psp_forge.h`

```c
// Collisioni 2D
bool forge_collide_rect_rect(ForgeRect a, ForgeRect b);
bool forge_collide_rect_circle(ForgeRect r, ForgeCircle c);
bool forge_collide_point_rect(float px, float py, ForgeRect r);

// Collisioni 3D
bool forge_collide_aabb_aabb(ForgeAABB a, ForgeAABB b);
bool forge_collide_sphere_sphere(ForgeSphere a, ForgeSphere b);
bool forge_collide_aabb_sphere(ForgeAABB b, ForgeSphere s);

// Calcolo dell'AABB orientata nello spazio mondo a partire dalla mesh
ForgeAABB forge_mesh_get_transformed_aabb(
    const ForgeMesh* mesh,
    float x, float y, float z,
    float sx, float sy, float sz
);
```

---

## 3. Implementazione nel Codice C99

### A. Rilevamento Ostacolo Solido (Rettangolo vs Rettangolo)
Per creare un ostacolo insormontabile, memorizziamo le coordinate precedenti del giocatore prima di applicare l'input. Se si verifica una sovrapposizione con l'ostacolo, ripristiniamo la posizione:

```c
float old_x = player_box.x;
float old_y = player_box.y;

// Applicazione movimento da input
player_box.x += move_x * speed * dt;
player_box.y += move_y * speed * dt;

// Controllo collisione
if (forge_collide_rect_rect(player_box, obstacle_box)) {
    // Blocco del movimento: annulla lo spostamento
    player_box.x = old_x;
    player_box.y = old_y;
}
```

### B. Raccolta di un Oggetto / Moneta (Rettangolo vs Cerchio)
Quando la hitbox rettangolare del giocatore tocca il raggio della moneta circolare:

```c
if (forge_collide_rect_circle(player_box, coin_circle)) {
    score++;
    forge_sound_play(chime_snd, 0);

    // Riposiziona la moneta in un nuovo punto della mappa
    coin_circle.x = 60.0f + (float)(rand() % 360);
    coin_circle.y = 40.0f + (float)(rand() % 190);
}
```

### C. Collisioni 3D tra Modelli
Per modelli 3D caricati tramite `forge_mesh_load()`, l'header del file `.p3d` include già i limiti AABB originali calcolati in fase di cooking. Per verificare se due modelli 3D si scontrano nel mondo di gioco:

```c
ForgeAABB vehicle_box = forge_mesh_get_transformed_aabb(
    vehicle_mesh, veh_x, veh_y, veh_z, 1.0f, 1.0f, 1.0f
);

ForgeAABB barrier_box = forge_mesh_get_transformed_aabb(
    barrier_mesh, bar_x, bar_y, bar_z, 1.0f, 1.0f, 1.0f
);

if (forge_collide_aabb_aabb(vehicle_box, barrier_box)) {
    // Gestione impatto 3D
}
```

---

## 4. Come Provare la Demo

```bash
cd demos/demo_collisions
psp-forge build
psp-forge run
```
- Muovi il player con il D-pad o lo Stick analogico.
- Prova ad urtare il blocco rosso: diventerà giallo e bloccherà il passaggio.
- Raccogli la moneta dorata per riprodurre il suono di chime e vederla riposizionare.
