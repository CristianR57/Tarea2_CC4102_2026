// main.cpp
// Tarea 2 CC4102: Arbol B y Arbol B^epsilon en memoria externa.
//
// Modos de uso:
//   ./tarea2 verificar [--datos datos.bin]
//       Imprime los primeros 50 pares de datos.bin (para compararlos con datos_sample.txt).
//   ./tarea2 experimento [--datos datos.bin] [--dir resultados] [--alpha 0.5]
//                        [--nmin 15] [--nmax 26] [--keep] [--append]
//       Para cada N = 2^e (nmin <= e <= nmax) construye el arbol B y los dos B^epsilon,
//       los guarda en disco, hace 50 busquedas por rango leyendo desde el archivo y
//       escribe las metricas en <dir>/resultados.csv.
//   ./tarea2 ejemplo E1 E2 [--l unix] [--datos datos.bin] [--dir resultados]
//       Construye un arbol B con N = 2^E1 y N = 2^E2 y ejecuta la MISMA busqueda por rango
//       [l, l + 604800] en ambos; guarda los resultados en <dir>/rango_<N>.csv.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "arbol_b.h"
#include "arbol_be.h"
#include "disco.h"

namespace fs = std::filesystem;
using Reloj = std::chrono::steady_clock;

// ----------------------------------------------------------------------------
// Constantes del experimento
// ----------------------------------------------------------------------------
constexpr int32_t LLAVE_MIN = 1546300800;   // minimo de las llaves
constexpr int32_t LLAVE_MAX = 1754006400;   // maximo de las llaves
constexpr int32_t ANCHO_RANGO = 604800;     // una semana en segundos
constexpr int NUM_BUSQUEDAS = 50;           // busquedas por arbol y por N
constexpr size_t TAM_LOTE = 1 << 16;        // pares leidos de datos.bin por lectura
constexpr uint64_t SEMILLA = 20260102;      // semilla fija: mismas consultas en todos los arboles

// Los dos arboles B^epsilon pedidos (c + lmax = 510).
using BeChico = ArbolBe<4, 506>;     // epsilon ~ 0.25
using BeGrande = ArbolBe<126, 384>;  // epsilon ~ 0.75

// ----------------------------------------------------------------------------
// Utilidades
// ----------------------------------------------------------------------------

// Segundos transcurridos desde t0.
static double segundosDesde(Reloj::time_point t0) {
    return std::chrono::duration<double>(Reloj::now() - t0).count();
}

// Resumen de un resultado de busqueda: cantidad de pares y un hash que no depende del orden.
// Sirve para comprobar que los tres arboles devuelven exactamente los mismos pares.
struct Resumen {
    uint64_t n = 0;
    uint64_t hash = 0;
};
static bool operator==(const Resumen& a, const Resumen& b) { return a.n == b.n && a.hash == b.hash; }

// Mezcla de bits (splitmix64). Entrada: x. Salida: hash de 64 bits.
static uint64_t mezclarBits(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Calcula el Resumen de un vector de pares (independiente del orden).
static Resumen resumir(const std::vector<Par>& v) {
    Resumen r;
    r.n = v.size();
    for (const Par& p : v) {
        uint32_t bits;
        std::memcpy(&bits, &p.valor, sizeof(bits));
        r.hash += mezclarBits((static_cast<uint64_t>(static_cast<uint32_t>(p.llave)) << 32) | bits);
    }
    return r;
}

// Ordena pares por llave (y por valor en caso de empate).
static void ordenarPares(std::vector<Par>& v) {
    std::sort(v.begin(), v.end(), [](const Par& a, const Par& b) {
        return a.llave != b.llave ? a.llave < b.llave : a.valor < b.valor;
    });
}

// ----------------------------------------------------------------------------
// LectorDatos: lee secuencialmente los pares (int, float) de datos.bin por lotes.
// ----------------------------------------------------------------------------
class LectorDatos {
    std::ifstream in_;

public:
    // Entrada: ruta de datos.bin.
    explicit LectorDatos(const std::string& ruta) : in_(ruta, std::ios::binary) {
        if (!in_) throw std::runtime_error("No se pudo abrir el archivo de datos: " + ruta);
    }

    // Lee hasta "max" pares. Entrada: lote (se redimensiona), max. Salida: pares leidos.
    size_t leerLote(std::vector<Par>& lote, size_t max) {
        lote.resize(max);
        in_.read(reinterpret_cast<char*>(lote.data()), static_cast<std::streamsize>(max * sizeof(Par)));
        size_t leidos = static_cast<size_t>(in_.gcount()) / sizeof(Par);
        lote.resize(leidos);
        return leidos;
    }
};

// Cantidad de pares que hay en datos.bin (tamano del archivo / 8 bytes).
static size_t cantidadPares(const std::string& ruta) {
    return static_cast<size_t>(fs::file_size(ruta)) / sizeof(Par);
}

// ----------------------------------------------------------------------------
// Construccion de un arbol (insercion uno a uno de los primeros n pares)
// ----------------------------------------------------------------------------

// Construye un arbol de tipo Arbol insertando los primeros n pares de datos.bin.
// Entrada: datos = ruta de datos.bin, n = cantidad de pares, args = argumentos del
//          constructor del arbol (p. ej. alpha para B^epsilon).
// Salida:  el arbol (en RAM) y, en "segundos", el tiempo total de insercion
//          (no incluye el tiempo de leer datos.bin).
template <typename Arbol, typename... Args>
std::unique_ptr<Arbol> construir(const std::string& datos, size_t n, double& segundos, Args&&... args) {
    auto arbol = std::make_unique<Arbol>(std::forward<Args>(args)...);
    LectorDatos fuente(datos);
    std::vector<Par> lote;
    size_t insertados = 0;
    segundos = 0.0;
    while (insertados < n) {
        size_t leidos = fuente.leerLote(lote, std::min(TAM_LOTE, n - insertados));
        if (leidos == 0) throw std::runtime_error("datos.bin tiene menos pares de los esperados");
        auto t0 = Reloj::now();
        for (const Par& p : lote) arbol->insertar(p);
        segundos += segundosDesde(t0);
        insertados += leidos;
    }
    return arbol;
}

// ----------------------------------------------------------------------------
// Resultados de un arbol para un N
// ----------------------------------------------------------------------------
struct Fila {
    std::string arbol;          // "B", "Be_0.25" o "Be_0.75"
    int n_exp = 0;              // N = 2^n_exp
    size_t n = 0;               // N nominal
    size_t n_real = 0;          // pares realmente insertados (= n salvo que datos.bin sea mas chico)
    double t_creacion = 0;      // segundos de insercion de los N pares
    uint64_t lecturas_c = 0;    // lecturas simuladas durante la construccion
    uint64_t escrituras_c = 0;  // escrituras simuladas durante la construccion
    size_t nodos = 0;           // nodos (bloques) usados
    uint64_t bytes = 0;         // tamano en disco (nodos * 4096)
    double t_guardado = 0;      // segundos en escribir el arbol al archivo binario
    double t_busqueda_ms = 0;   // tiempo promedio de una busqueda (ms)
    double ios_busqueda = 0;    // lecturas de bloque promedio por busqueda
    double resultados = 0;      // pares encontrados en promedio por busqueda
    std::vector<Resumen> resumenes;  // resumen de cada una de las busquedas
};

// Encabezado del CSV de resultados.
static void escribirEncabezado(std::ostream& o) {
    o << "arbol,n_exp,n,n_real,t_creacion_s,lecturas_creacion,escrituras_creacion,ios_creacion,"
         "nodos,bytes,t_guardado_s,t_busqueda_ms,ios_busqueda,resultados_prom\n";
}

// Escribe una fila del CSV. Entrada: stream de salida y la fila.
static void escribirFila(std::ostream& o, const Fila& f) {
    o << f.arbol << ',' << f.n_exp << ',' << f.n << ',' << f.n_real << ',' << std::setprecision(9)
      << f.t_creacion << ',' << f.lecturas_c << ',' << f.escrituras_c << ','
      << (f.lecturas_c + f.escrituras_c) << ',' << f.nodos << ',' << f.bytes << ',' << f.t_guardado << ','
      << f.t_busqueda_ms << ',' << f.ios_busqueda << ',' << f.resultados << '\n';
}

// Construye un arbol, lo guarda en disco, y hace las busquedas leyendo desde el archivo.
// Entrada: nombre = etiqueta del arbol, e = exponente (N = 2^e), nReal = pares a insertar,
//          datos = ruta de datos.bin, ls = limites inferiores de las consultas,
//          rutaArbol = archivo donde se guarda el arbol, mantener = no borrar el archivo,
//          args = argumentos del constructor del arbol.
// Salida:  Fila con todas las metricas.
template <typename Arbol, typename... Args>
Fila correr(const std::string& nombre, int e, size_t nReal, const std::string& datos,
            const std::vector<int32_t>& ls, const std::string& rutaArbol, bool mantener, Args&&... args) {
    Fila f;
    f.arbol = nombre;
    f.n_exp = e;
    f.n = static_cast<size_t>(1) << e;
    f.n_real = nReal;

    // 1) Construccion en RAM (disco simulado).
    auto arbol = construir<Arbol>(datos, nReal, f.t_creacion, std::forward<Args>(args)...);
    f.lecturas_c = arbol->disco.lecturas;
    f.escrituras_c = arbol->disco.escrituras;
    f.nodos = arbol->disco.cantidad();
    f.bytes = static_cast<uint64_t>(f.nodos) * TAM_BLOQUE;

    // 2) Guardado en archivo binario (un nodo por bloque, secuencial).
    auto t0 = Reloj::now();
    arbol->guardar(rutaArbol);
    f.t_guardado = segundosDesde(t0);
    arbol.reset();  // libera la RAM antes de buscar

    // 3) Busquedas por rango leyendo desde el archivo.
    {
        LectorArchivo<typename Arbol::Nodo> lector(rutaArbol);
        std::vector<Par> salida;
        double tTotal = 0;
        uint64_t iosTotal = 0, encontrados = 0;
        for (int32_t l : ls) {
            salida.clear();
            lector.lecturas = 0;
            auto ti = Reloj::now();
            Arbol::buscarRango(lector, 0, l, l + ANCHO_RANGO, salida);
            tTotal += segundosDesde(ti);
            iosTotal += lector.lecturas;
            encontrados += salida.size();
            f.resumenes.push_back(resumir(salida));
        }
        const double q = static_cast<double>(ls.size());
        f.t_busqueda_ms = tTotal / q * 1000.0;
        f.ios_busqueda = static_cast<double>(iosTotal) / q;
        f.resultados = static_cast<double>(encontrados) / q;
    }
    if (!mantener) fs::remove(rutaArbol);
    return f;
}

// ----------------------------------------------------------------------------
// Opciones de linea de comandos
// ----------------------------------------------------------------------------
struct Opciones {
    std::string modo;
    std::string datos = "datos.bin";
    std::string dir = "resultados";
    double alpha = 0.5;
    int nmin = 15, nmax = 26;
    bool keep = false, append = false;
    int32_t l = 1650000000;  // consulta del modo "ejemplo"
    std::vector<std::string> pos;
};

// Interpreta argv. Entrada: argc, argv. Salida: Opciones.
static Opciones leerOpciones(int argc, char** argv) {
    Opciones o;
    if (argc >= 2) o.modo = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto valor = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Falta el valor de " + a);
            return argv[++i];
        };
        if (a == "--datos") o.datos = valor();
        else if (a == "--dir") o.dir = valor();
        else if (a == "--alpha") o.alpha = std::stod(valor());
        else if (a == "--nmin") o.nmin = std::stoi(valor());
        else if (a == "--nmax") o.nmax = std::stoi(valor());
        else if (a == "--l") o.l = static_cast<int32_t>(std::stol(valor()));
        else if (a == "--keep") o.keep = true;
        else if (a == "--append") o.append = true;
        else o.pos.push_back(a);
    }
    return o;
}

// ----------------------------------------------------------------------------
// Modo "verificar": imprime los primeros 50 pares de datos.bin
// ----------------------------------------------------------------------------
static int verificar(const Opciones& o) {
    LectorDatos fuente(o.datos);
    std::vector<Par> v;
    fuente.leerLote(v, 50);
    std::printf("Primeros %zu pares de %s (comparar con datos_sample.txt):\n", v.size(), o.datos.c_str());
    for (const Par& p : v) std::printf("%d %.6f\n", p.llave, static_cast<double>(p.valor));
    std::printf("Total de pares en el archivo: %zu\n", cantidadPares(o.datos));
    return 0;
}

// ----------------------------------------------------------------------------
// Modo "experimento"
// ----------------------------------------------------------------------------
static int experimento(const Opciones& o) {
    const size_t total = cantidadPares(o.datos);
    fs::create_directories(o.dir);
    std::cout << "datos.bin: " << total << " pares. alpha = " << o.alpha << "\n";

    // Mismas 50 consultas para todos los arboles y todos los N.
    std::mt19937_64 rng(SEMILLA);
    std::uniform_int_distribution<int32_t> dist(LLAVE_MIN, LLAVE_MAX);
    std::vector<int32_t> ls(NUM_BUSQUEDAS);
    for (auto& l : ls) l = dist(rng);

    const std::string rutaCsv = (fs::path(o.dir) / "resultados.csv").string();
    const bool nuevo = !o.append || !fs::exists(rutaCsv);
    std::ofstream csv(rutaCsv, nuevo ? std::ios::trunc : std::ios::app);
    if (!csv) throw std::runtime_error("No se pudo abrir " + rutaCsv);
    if (nuevo) escribirEncabezado(csv);

    const std::string rb = (fs::path(o.dir) / "arbol_b.bin").string();
    const std::string r25 = (fs::path(o.dir) / "arbol_be_025.bin").string();
    const std::string r75 = (fs::path(o.dir) / "arbol_be_075.bin").string();

    for (int e = o.nmin; e <= o.nmax; ++e) {
        const size_t nNominal = static_cast<size_t>(1) << e;
        const size_t nReal = std::min(nNominal, total);
        std::cout << "\n=== N = 2^" << e << " = " << nNominal << " ===\n";
        if (nReal < nNominal)
            std::cout << "AVISO: datos.bin solo tiene " << total << " pares; se insertan " << nReal << ".\n";

        std::vector<Fila> filas;
        filas.push_back(correr<ArbolB>("B", e, nReal, o.datos, ls, rb, o.keep));
        filas.push_back(correr<BeChico>("Be_0.25", e, nReal, o.datos, ls, r25, o.keep, o.alpha));
        filas.push_back(correr<BeGrande>("Be_0.75", e, nReal, o.datos, ls, r75, o.keep, o.alpha));

        for (const Fila& f : filas) {
            escribirFila(csv, f);
            csv.flush();
            std::printf("%-8s creacion %9.3f s | I/Os %12llu | nodos %9zu | busqueda %9.3f ms, %9.1f I/Os, %9.1f pares\n",
                        f.arbol.c_str(), f.t_creacion,
                        static_cast<unsigned long long>(f.lecturas_c + f.escrituras_c), f.nodos,
                        f.t_busqueda_ms, f.ios_busqueda, f.resultados);
        }

        // Comprobacion de correctitud: los tres arboles deben devolver lo mismo.
        bool ok = true;
        for (size_t k = 1; k < filas.size(); ++k)
            for (size_t q = 0; q < ls.size(); ++q)
                if (!(filas[0].resumenes[q] == filas[k].resumenes[q])) {
                    ok = false;
                    std::cout << "!! " << filas[k].arbol << " difiere de B en la consulta " << q << "\n";
                }
        std::cout << (ok ? "Verificacion: los 3 arboles devuelven los mismos resultados.\n"
                         : "Verificacion: HAY DIFERENCIAS entre arboles.\n");
    }
    std::cout << "\nListo. Resultados en " << rutaCsv << "\n";
    return 0;
}

// ----------------------------------------------------------------------------
// Modo "ejemplo": misma busqueda por rango para dos valores de N (arbol B)
// ----------------------------------------------------------------------------
static int ejemplo(const Opciones& o) {
    if (o.pos.size() < 2) throw std::runtime_error("Uso: ejemplo E1 E2 [--l unix]");
    fs::create_directories(o.dir);
    const size_t total = cantidadPares(o.datos);
    const int32_t l = o.l, u = o.l + ANCHO_RANGO;
    const std::string rutaArbol = (fs::path(o.dir) / "arbol_ejemplo.bin").string();

    for (int k = 0; k < 2; ++k) {
        const int e = std::stoi(o.pos[k]);
        const size_t n = std::min(static_cast<size_t>(1) << e, total);
        double seg = 0;
        auto arbol = construir<ArbolB>(o.datos, n, seg);
        arbol->guardar(rutaArbol);
        arbol.reset();

        std::vector<Par> salida;
        {
            LectorArchivo<NodoB> lector(rutaArbol);
            ArbolB::buscarRango(lector, 0, l, u, salida);
        }
        ordenarPares(salida);
        const std::string rutaCsv = (fs::path(o.dir) / ("rango_" + std::to_string(e) + ".csv")).string();
        std::ofstream csv(rutaCsv);
        csv << "llave,valor\n";
        for (const Par& p : salida) csv << p.llave << ',' << std::setprecision(7) << p.valor << '\n';
        std::cout << "N = 2^" << e << ": " << salida.size() << " pares en [" << l << ", " << u << "] -> "
                  << rutaCsv << "\n";
    }
    fs::remove(rutaArbol);
    return 0;
}

int main(int argc, char** argv) {
    try {
        Opciones o = leerOpciones(argc, argv);
        if (o.modo == "verificar") return verificar(o);
        if (o.modo == "experimento") return experimento(o);
        if (o.modo == "ejemplo") return ejemplo(o);
        std::cout << "Uso:\n"
                     "  tarea2 verificar [--datos datos.bin]\n"
                     "  tarea2 experimento [--datos datos.bin] [--dir resultados] [--alpha 0.5]\n"
                     "                     [--nmin 15] [--nmax 26] [--keep] [--append]\n"
                     "  tarea2 ejemplo E1 E2 [--l unix] [--datos datos.bin] [--dir resultados]\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
