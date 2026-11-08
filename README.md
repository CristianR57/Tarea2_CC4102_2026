# Tarea2-CC4102

Implementación de un **árbol B** y de dos **árboles B^ε** (ε ≈ 0.25 y ε ≈ 0.75) en memoria externa, con inserción de pares llave-valor y búsqueda por rango, usando datos reales de temperatura de la Estación Quinta Normal (Santiago, 2019–2025). Un único `main` corre toda la batería de experimentos sin tener que modificar el código.

## Requisitos

* Compilador de C++.
* Python 3 con **matplotlib** (solo para graficar).
* Los archivos `datos.bin` y `datos_sample.txt` entregados por el equipo docente, en la misma carpeta que el código.
* Espacio libre: ~2 GB en disco y ~2 GB de RAM para el mayor N (2^26).

## Archivos del proyecto

| Archivo | Qué hace |
|---|---|
| `disco.h` | `DiscoSimulado`: arreglo de nodos en RAM que simula el disco, cuenta lecturas/escrituras y guarda el árbol en un binario. `LectorArchivo`: lee nodos de 4096 bytes desde el binario y cuenta las lecturas. Define `Par` (llave `int`, valor `float`). |
| `arbol_b.h` | Árbol B (b = 340): nodo, split, inserción y búsqueda por rango. |
| `arbol_be.h` | Árbol B^ε: nodo, split de hoja e interno, vaciado de buffer (flush), inserción y búsqueda por rango. Parametrizado por `c` y `lmax`. |
| `main.cpp` | Lee `datos.bin`, corre los experimentos, mide tiempos e I/Os, verifica resultados y escribe los CSV. |
| `graficos.py` | Lee los CSV y genera los gráficos. |
| `datos.bin` | (entregado) 67 millones de pares: llave `int` (tiempo Unix) y valor `float` (temperatura). |
| `datos_sample.txt` | (entregado) Primeros 50 pares en texto, para verificar la lectura. |

## Cómo compilar y ejecutar todo

Con todos los `.h` y `main.cpp` en la misma carpeta:

```bash
g++ -O2 -std=c++17 -Wall -o tarea2 main.cpp 
./tarea2 experimento --alpha 0.5            # experimento completo (2^15 a 2^26)
./tarea2 ejemplo 18 24                      # misma búsqueda para dos N
python graficos.py resultados               # genera los gráficos (PNG)
```

Esto corre, para cada N = 2^15, 2^16, …, 2^26, la construcción y las búsquedas del árbol B y de los dos B^ε (c = 4, lmax = 506 y c = 126, lmax = 384), con α = 0.5, y por ultimo, la busqueda por rango para dos N dados. **Puede tardar del orden de 5 minutos**.

Al terminar se genera la carpeta `resultados/graficos/` con las imágenes `.png` (ver sección siguiente).

> El ejecutable se llama `tarea2.exe`.

## Pasos recomendados (de menor a mayor)

1. **Compilar el archivo `main.cpp`**:
   ```bash
   g++ -O2 -std=c++17 -Wall -o tarea2 main.cpp 
   ```
2. **Verificar que los datos se leen bien** (comparar con `datos_sample.txt`):
   ```bash
   ./tarea2 verificar
   ```
3. **Prueba rápida** (N = 2^15 a 2^18), para comprobar que todo compila y corre:
   ```bash
   ./tarea2 experimento --nmin 15 --nmax 18
   ```
4. **Experimento completo con alpha = 0.5**:
   ```bash
   ./tarea2 experimento --alpha 0.5  
   ```
5. **Búsqueda por rango de ejemplo** para dos valores de N (por ejemplo 2^18 y 2^24):
   ```bash
   ./tarea2 ejemplo 18 24
   ```
6. **Gráficos**:
   ```bash
   python graficos.py resultados
   ```

## Opciones de `./tarea2`

### `verificar`
Imprime los primeros 50 pares de `datos.bin` y el total de pares del archivo.

### `experimento`
Se pueden combinar entre sí:

* `--datos ruta`: ruta de `datos.bin` (por defecto `datos.bin`).
* `--dir carpeta`: carpeta de salida (por defecto `resultados`).
* `--alpha a`: parámetro α del vaciado de buffer, con 0 < a < 1 (por defecto **0.5**).
* `--nmin e` / `--nmax e`: exponentes mínimo y máximo, con N = 2^e (por defecto 15 y 26).
* `--keep`: no borrar los archivos binarios de los árboles al terminar.
* `--append`: agregar filas a `resultados.csv` en vez de reescribirlo (útil para continuar una corrida interrumpida).

Ejemplo: `./tarea2 experimento --alpha 0.25 --nmin 20 --nmax 22 --dir prueba`

### `ejemplo E1 E2`
Construye un árbol B con N = 2^E1 y con N = 2^E2 y ejecuta la **misma** búsqueda por rango (una semana) en ambos.

* `--l unix`: inicio del rango en tiempo Unix (por defecto 1650000000). El rango es `[l, l + 604800]`.
* Acepta también `--datos` y `--dir`.

## Archivos que genera

| Archivo | Contenido |
|---|---|
| `resultados/resultados.csv` | Una fila por (árbol, N): tiempo de creación, lecturas y escrituras simuladas, I/Os totales de creación, nodos usados, tamaño en bytes, tiempo de guardado a disco, tiempo promedio de búsqueda (ms), I/Os promedio por búsqueda y cantidad promedio de pares encontrados. Los árboles se llaman `B`, `Be_0.25` y `Be_0.75`. |
| `resultados/rango_<e>.csv` | Pares (llave, valor) devueltos por la búsqueda de ejemplo para N = 2^e. |
| `resultados/graficos/tiempo_creacion.png` | Tiempo de creación vs. N. |
| `resultados/graficos/ios_creacion.png` | I/Os simuladas de inserción vs. N. |
| `resultados/graficos/tamano.png` | Tamaño del árbol vs. N. |
| `resultados/graficos/tiempo_busqueda.png` | Tiempo promedio de búsqueda vs. N. |
| `resultados/graficos/ios_busqueda.png` | I/Os promedio por búsqueda vs. N. |
| `resultados/graficos/rango_comparacion.png` | Temperatura vs. tiempo de la misma búsqueda para dos N distintos. |

## Detalles de implementación

* **Disco simulado:** durante la construcción el árbol es un arreglo de nodos en RAM (`DiscoSimulado`); el nodo 0 es la raíz. Cada `leer` cuenta como una lectura y cada `escribir`/`agregar`/`registrarEscritura` como una escritura. Al terminar se escribe el arreglo a un binario (nodo *i* en la posición *i*·4096).
* **Búsquedas:** se hacen leyendo bloques de 4096 bytes desde ese binario (`LectorArchivo`), contando una lectura por bloque. El sistema operativo puede cachear el archivo, por lo que los tiempos pueden ser optimistas; las I/Os contadas no se ven afectadas.
* **Consultas:** 50 límites inferiores `l` uniformes en [1546300800, 1754006400] (semilla fija) y `u = l + 604800`. Las mismas 50 consultas se usan en los tres árboles y para todos los N.
* **Parámetro α:** se bajan del buffer los grupos con al menos ⌈f·α⌉ elementos, con f = ⌈k_buffer / (k_llaves + 1)⌉. Valor usado por defecto: α = 0.5.
* **Memoria:** los árboles se construyen, guardan y consultan de a uno, liberando la RAM entre uno y otro. Los datos se leen por lotes de 65536 pares y esa lectura no se incluye en el tiempo de creación.
* **Verificación automática:** para cada N, el programa compara los resultados de las 50 búsquedas del árbol B con los de los dos B^ε (cantidad de pares y hash independiente del orden) e imprime si coinciden. Con rangos de una semana se espera encontrar cerca del 0.29 % de los N pares.
* Si `datos.bin` tuviera menos de 2^26 pares, el último N usa todos los pares disponibles (se avisa por consola y la columna `n_real` del CSV lo refleja).
