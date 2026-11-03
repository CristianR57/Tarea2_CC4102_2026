// disco.h
// Simulacion de disco (arreglo de nodos en RAM con conteo de I/Os) y lector
// de nodos desde un archivo binario (con conteo de lecturas).
#ifndef DISCO_H
#define DISCO_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Tamano de un bloque de disco = tamano de un nodo (bytes).
constexpr size_t TAM_BLOQUE = 4096;

// Par llave-valor: llave = tiempo Unix (int, 4 bytes), valor = temperatura (float, 4 bytes).
struct Par {
    int32_t llave;
    float valor;
};
static_assert(sizeof(Par) == 8, "Par debe medir 8 bytes");

// ---------------------------------------------------------------------------
// DiscoSimulado<Nodo>
// Arreglo de nodos en RAM que simula el disco durante la construccion del arbol.
//  - El nodo 0 es siempre la raiz.
//  - Cada acceso de lectura (leer) cuenta como 1 lectura.
//  - Cada escritura (escribir / agregar / registrarEscritura) cuenta como 1 escritura.
// Los nodos se guardan en "chunks" de 1024 nodos para que las referencias devueltas
// por leer() sigan siendo validas aunque el arreglo crezca (sin reubicar memoria).
// Uso tipico: Nodo& n = disco.leer(i); modificar n; disco.registrarEscritura();
// ---------------------------------------------------------------------------
template <typename Nodo>
class DiscoSimulado {
    static_assert(sizeof(Nodo) == TAM_BLOQUE, "Cada nodo debe medir exactamente 4096 bytes");
    static constexpr size_t BITS = 10;
    static constexpr size_t TAM_CHUNK = size_t(1) << BITS;
    static constexpr size_t MASCARA = TAM_CHUNK - 1;

    std::vector<std::unique_ptr<Nodo[]>> chunks_;
    size_t n_ = 0;

public:
    uint64_t lecturas = 0;    // lecturas simuladas acumuladas
    uint64_t escrituras = 0;  // escrituras simuladas acumuladas

    // Cantidad de nodos (bloques) usados.
    size_t cantidad() const { return n_; }

    // Lee el nodo en la posicion i (cuenta 1 lectura).
    // Entrada: i = indice del nodo. Salida: referencia al nodo en el "disco".
    // Si se modifica el nodo, debe llamarse luego a registrarEscritura().
    Nodo& leer(size_t i) {
        ++lecturas;
        return chunks_[i >> BITS][i & MASCARA];
    }

    // Agrega un nodo al final del arreglo (cuenta 1 escritura).
    // Entrada: nodo a copiar. Salida: indice donde quedo.
    size_t agregar(const Nodo& nodo) {
        if ((n_ >> BITS) == chunks_.size()) chunks_.emplace_back(new Nodo[TAM_CHUNK]());
        chunks_[n_ >> BITS][n_ & MASCARA] = nodo;
        ++escrituras;
        return n_++;
    }

    // Sobrescribe el nodo i con "nodo" (cuenta 1 escritura).
    void escribir(size_t i, const Nodo& nodo) {
        chunks_[i >> BITS][i & MASCARA] = nodo;
        ++escrituras;
    }

    // Cuenta 1 escritura de un nodo que ya fue modificado "in place" via leer().
    void registrarEscritura() { ++escrituras; }

    // Guarda el arbol completo en un archivo binario: nodo i en la posicion i*4096.
    // Entrada: ruta del archivo de salida.
    void guardar(const std::string& ruta) const {
        std::ofstream out(ruta, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("No se pudo abrir para escribir: " + ruta);
        size_t restantes = n_;
        for (size_t c = 0; restantes > 0; ++c) {
            size_t cuantos = std::min(restantes, TAM_CHUNK);
            out.write(reinterpret_cast<const char*>(chunks_[c].get()),
                      static_cast<std::streamsize>(cuantos * TAM_BLOQUE));
            restantes -= cuantos;
        }
        if (!out) throw std::runtime_error("Error escribiendo: " + ruta);
    }
};

// ---------------------------------------------------------------------------
// LectorArchivo<Nodo>
// Lee nodos desde el archivo binario del arbol (posicion i * 4096) y cuenta
// cada lectura de bloque. Se usa para las busquedas por rango.
// ---------------------------------------------------------------------------
template <typename Nodo>
class LectorArchivo {
    std::ifstream in_;

public:
    uint64_t lecturas = 0;  // lecturas de bloque realizadas

    // Entrada: ruta del archivo binario del arbol.
    explicit LectorArchivo(const std::string& ruta) : in_(ruta, std::ios::binary) {
        if (!in_) throw std::runtime_error("No se pudo abrir: " + ruta);
    }

    // Lee el nodo i del archivo en "destino" (cuenta 1 lectura).
    void leer(size_t i, Nodo& destino) {
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(i) * static_cast<std::streamoff>(TAM_BLOQUE));
        in_.read(reinterpret_cast<char*>(&destino), static_cast<std::streamsize>(TAM_BLOQUE));
        if (!in_) throw std::runtime_error("Error leyendo nodo del archivo");
        ++lecturas;
    }
};

#endif  // DISCO_H
