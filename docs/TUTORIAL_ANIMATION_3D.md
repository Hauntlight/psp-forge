# Tutorial: Animazione 3D Procedurale & Gerarchica 💎

Questo tutorial spiega come implementare animazioni 3D su Sony PSP sfruttando lo stack di matrici hardware (`pspgum`) e calcoli matematici ad alte prestazioni, garantendo i 60 FPS senza sovraccaricare la CPU MIPS.

La demo completa e funzionante associata a questa guida si trova in:
`demos/demo_anim_3d/`

---

## 1. Perché l'Animazione Procedurale & Gerarchica su PSP?

L'hardware della PSP possiede una Vector Floating Point Unit (VFPU) e un Graphics Engine con supporto per trasformazioni hardware $4 \times 4$.
Caricare e riprodurre complessi scheletri scheletrici con pesi di vertici per CPU (*software vertex skinning*) consuma preziosi cicli CPU. L'approccio vincente e largamente utilizzato nei migliori titoli commerciali PSP consiste nel combinare:

1. **Animazioni Procedurali con Funzioni Armoniche**:
   - Fluttuazione/Bobbing verticale: $Y(t) = Y_0 + \sin(t \cdot \omega) \cdot A$
   - Rollio/Beccheggio dinamico (es. oscillazione barca, velivolo, gemma): $\theta(t) = \cos(t \cdot \omega) \cdot \alpha$
   - Rotazione continua (ruote, eliche, oggetti collezionabili): $R_y(t) = t \cdot \text{speed}$
2. **Animazione a Nodi Gerarchici**:
   - Invece di deformare i singoli vertici, il modello viene diviso in sottomesh logiche collegate da trasformazioni a cascata tramite `sceGumPushMatrix()` / `sceGumPopMatrix()`.

---

## 2. Preparazione dei Modelli 3D (`.obj` $\rightarrow$ `.p3d`)

Nel nostro esempio abbiamo due entità distinte:
- `pedestal.obj`: Piedistallo statico alla base.
- `gem.obj`: Cristallo sfaccettato fluttuante centrato sull'origine $(0, 0, 0)$.

Entrambi i modelli, quando elaborati da `psp-forge cook`, vengono convertiti nel formato binario compatto `.p3d` con vertici allineati a 16 byte per il DMA della GPU e bounding box AABB precalcolata.

---

## 3. Implementazione nel Codice C99

### A. Calcolo delle Traiettorie nel Game Loop
```c
float dt = forge_get_delta_time();
time_acc += dt;

// 1. Oscillazione verticale (frequenza 2.5 rad/s, ampiezza 0.35 unità)
float gem_bob_y = sinf(time_acc * 2.5f) * 0.35f;

// 2. Rotazione continua attorno all'asse Y
float gem_rot_y = time_acc * 2.0f;

// 3. Inclinazione ritmica di beccheggio sull'asse X
float gem_tilt  = cosf(time_acc * 1.5f) * 0.15f;
```

### B. Telecamera Orbitale Mobile
Possiamo impostare una telecamera in coordinate polari che ruota fluidamente attorno all'oggetto su comando del D-Pad o dello Stick analogico:
```c
float cam_x = sinf(cam_angle) * cam_dist;
float cam_z = -cosf(cam_angle) * cam_dist;

forge_set_camera(
    cam_x, cam_height, cam_z, // Posizione telecamera (Eye)
    0.0f, 0.0f, 0.0f,         // Punto osservato (Target / Centro scena)
    60.0f                     // Angolo di campo (FOV)
);
```

### C. Rendering con `forge_draw_mesh`
Passiamo i parametri calcolati direttamente alla pipeline di trasformazione della GPU:
```c
forge_begin_frame();
forge_clear(0xFF140F0A);

// Disegno del piedistallo fisso alla base
forge_draw_mesh(ped_mesh, ped_tex, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

// Disegno del cristallo animato
forge_draw_mesh(
    gem_mesh, gem_tex,
    0.0f, 0.5f + gem_bob_y, 0.0f,  // Posizione Y animata armonicamente
    gem_tilt, gem_rot_y, 0.0f,      // Rotazione Y + Beccheggio X
    1.0f, 1.0f, 1.0f               // Scala
);

forge_end_frame();
```

---

## 4. Come Compilare ed Eseguire la Demo

```bash
cd demos/demo_anim_3d
psp-forge build
psp-forge run
```
