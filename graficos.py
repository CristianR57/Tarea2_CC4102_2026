#!/usr/bin/env python3
"""Genera los graficos de la Tarea 2 (CC4102) a partir de los CSV que escribe tarea2.

Uso:
    python graficos.py [directorio]      (por defecto: resultados)

Lee:
    <dir>/resultados.csv   -> graficos de metricas en funcion de N (modo "experimento")
    <dir>/rango_<e>.csv    -> grafico de una misma busqueda por rango para dos N (modo "ejemplo")
Escribe los PNG en <dir>/graficos/.
Dependencia: matplotlib (pip install matplotlib).
"""
import csv
import glob
import os
import re
import sys
from datetime import datetime, timezone

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

try:
    from zoneinfo import ZoneInfo
    ZONA = ZoneInfo("America/Santiago")
except Exception:  # sin base de datos de zonas horarias: se usa UTC
    ZONA = timezone.utc

ESTILOS = {"B": ("tab:blue", "o"), "Be_0.25": ("tab:orange", "s"), "Be_0.75": ("tab:green", "^")}
ETIQUETAS = {"B": "Arbol B", "Be_0.25": "B^ε (ε≈0.25)", "Be_0.75": "B^ε (ε≈0.75)"}


def leer_resultados(ruta):
    """Lee resultados.csv. Entrada: ruta. Salida: dict arbol -> lista de filas (dict), ordenadas por N."""
    datos = {}
    with open(ruta, newline="") as f:
        for fila in csv.DictReader(f):
            datos.setdefault(fila["arbol"], []).append(fila)
    for filas in datos.values():
        filas.sort(key=lambda r: int(r["n_exp"]))
    return datos


def grafico_metrica(datos, columna, titulo, ylabel, archivo, log_y=True, escala=1.0):
    """Dibuja una metrica vs N para los tres arboles.
    Entrada: datos (de leer_resultados), columna del CSV, titulo, ylabel, archivo de salida,
             log_y (escala log en y), escala (factor multiplicativo para cambiar unidades)."""
    fig, ax = plt.subplots(figsize=(7, 4.5))
    exps = set()
    for arbol, filas in datos.items():
        color, marca = ESTILOS.get(arbol, (None, "o"))
        xs = [int(r["n_exp"]) for r in filas]
        ys = [float(r[columna]) * escala for r in filas]
        exps.update(xs)
        ax.plot(xs, ys, marker=marca, color=color, label=ETIQUETAS.get(arbol, arbol))
    ax.set_xticks(sorted(exps))
    ax.set_xticklabels([f"$2^{{{e}}}$" for e in sorted(exps)])
    ax.set_xlabel("N (cantidad de pares insertados)")
    ax.set_ylabel(ylabel)
    ax.set_title(titulo)
    if log_y:
        ax.set_yscale("log")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(archivo, dpi=150)
    plt.close(fig)
    print("Guardado", archivo)


def graficos_metricas(dir_res, dir_out):
    ruta = os.path.join(dir_res, "resultados.csv")
    if not os.path.exists(ruta):
        print("No existe", ruta, "(se omiten los graficos de metricas)")
        return
    datos = leer_resultados(ruta)
    g = lambda nombre: os.path.join(dir_out, nombre)
    grafico_metrica(datos, "t_creacion_s", "Tiempo de creación del árbol", "segundos", g("tiempo_creacion.png"))
    grafico_metrica(datos, "ios_creacion", "I/Os simuladas al insertar N pares (lecturas + escrituras)",
                    "I/Os", g("ios_creacion.png"))
    grafico_metrica(datos, "bytes", "Tamaño del árbol en disco", "MiB", g("tamano.png"), escala=1 / 2**20)
    grafico_metrica(datos, "t_busqueda_ms", "Tiempo promedio por búsqueda por rango (1 semana)",
                    "milisegundos", g("tiempo_busqueda.png"))
    grafico_metrica(datos, "ios_busqueda", "I/Os promedio por búsqueda por rango (1 semana)",
                    "lecturas de bloque", g("ios_busqueda.png"))


def graficos_rango(dir_res, dir_out):
    """Grafica temperatura vs tiempo de la misma busqueda para dos valores de N."""
    archivos = glob.glob(os.path.join(dir_res, "rango_*.csv"))
    archivos = sorted(archivos, key=lambda a: int(re.findall(r"rango_(\d+)\.csv", a)[0]))
    if len(archivos) < 2:
        print("Se necesitan dos archivos rango_*.csv (se omite el grafico de rango)")
        return
    archivos = archivos[:2]
    fig, ejes = plt.subplots(len(archivos), 1, figsize=(9, 3.6 * len(archivos)), sharex=True, sharey=True)
    for ax, ruta in zip(ejes, archivos):
        e = int(re.findall(r"rango_(\d+)\.csv", ruta)[0])
        xs, ys = [], []
        with open(ruta, newline="") as f:
            for fila in csv.DictReader(f):
                xs.append(datetime.fromtimestamp(int(fila["llave"]), tz=ZONA))
                ys.append(float(fila["valor"]))
        ax.plot(xs, ys, ".", markersize=2, color="tab:red")
        ax.set_title(f"N = $2^{{{e}}}$  ({len(xs)} mediciones en el rango)")
        ax.set_ylabel("Temperatura (°C)")
        ax.grid(True, alpha=0.3)
    ejes[-1].set_xlabel("Tiempo")
    fig.autofmt_xdate()
    fig.tight_layout()
    salida = os.path.join(dir_out, "rango_comparacion.png")
    fig.savefig(salida, dpi=150)
    plt.close(fig)
    print("Guardado", salida)


if __name__ == "__main__":
    dir_res = sys.argv[1] if len(sys.argv) > 1 else "resultados"
    dir_out = os.path.join(dir_res, "graficos")
    os.makedirs(dir_out, exist_ok=True)
    graficos_metricas(dir_res, dir_out)
    graficos_rango(dir_res, dir_out)
