// arbol_b.h
// Arbol B en memoria externa (nodos de 4096 bytes, b = 340).
#ifndef ARBOL_B_H
#define ARBOL_B_H

#include <algorithm>
#include <climits>
#include <vector>

#include "disco.h"

// Maximo de pares llave-valor por nodo.
constexpr int B_MAX = 340;

// Nodo del arbol B: 4 + 4 + 340*8 + 341*4 + 4 = 4096 bytes.
struct NodoB {
    int32_t es_interno;                 // 1 = interno, 0 = hoja
    int32_t k;                          // pares actuales (b/2 - 1 <= k <= b)
    Par llaves_valores[B_MAX];          // pares ordenados ascendentemente por llave
    int32_t hijos[B_MAX + 1];           // posiciones de los k+1 hijos (ignorado en hojas)
    int32_t padding;                    // sin uso (0)
};
static_assert(sizeof(NodoB) == TAM_BLOQUE, "NodoB debe medir 4096 bytes");

// Split de un nodo lleno (k == b).
// Entrada: u = nodo lleno.
// Salida: izq (b/2 - 1 pares), der (b/2 pares) y el par mediano (posicion b/2 contando desde 1).
//         Si u es interno, izq recibe los primeros b/2 hijos y der los b/2 + 1 restantes.
inline void dividirNodoB(const NodoB& u, NodoB& izq, NodoB& der, Par& mediano) {
    const int m = B_MAX / 2;  // 170
    izq = NodoB{};
    der = NodoB{};
    izq.es_interno = der.es_interno = u.es_interno;
    mediano = u.llaves_valores[m - 1];
    izq.k = m - 1;
    der.k = B_MAX - m;
    std::copy(u.llaves_valores, u.llaves_valores + (m - 1), izq.llaves_valores);
    std::copy(u.llaves_valores + m, u.llaves_valores + B_MAX, der.llaves_valores);
    if (u.es_interno) {
        std::copy(u.hijos, u.hijos + m, izq.hijos);
        std::copy(u.hijos + m, u.hijos + B_MAX + 1, der.hijos);
    }
}

class ArbolB {
public:
    using Nodo = NodoB;
    DiscoSimulado<NodoB> disco;  // arreglo de nodos en RAM; disco.leer(0) es la raiz

    // Crea un arbol vacio (raiz = hoja vacia en la posicion 0).
    ArbolB() {
        NodoB raiz{};
        disco.agregar(raiz);
    }

    // Inserta un par llave-valor (split preventivo de nodos llenos, de arriba hacia abajo).
    // Entrada: p = par a insertar.
    void insertar(const Par& p) {
        size_t idx = 0;
        NodoB* nodo = &disco.leer(0);

        // Raiz llena: split, nueva raiz en la posicion 0, nodos nuevos al final.
        if (nodo->k == B_MAX) {
            NodoB izq, der;
            Par med;
            dividirNodoB(*nodo, izq, der, med);
            size_t iizq = disco.agregar(izq);
            size_t ider = disco.agregar(der);
            NodoB nueva{};
            nueva.es_interno = 1;
            nueva.k = 1;
            nueva.llaves_valores[0] = med;
            nueva.hijos[0] = static_cast<int32_t>(iizq);
            nueva.hijos[1] = static_cast<int32_t>(ider);
            disco.escribir(0, nueva);
            idx = (p.llave <= med.llave) ? iizq : ider;
            nodo = &disco.leer(idx);
        }

        // Descenso: nunca se entra a un nodo lleno.
        while (nodo->es_interno) {
            int i = static_cast<int>(
                std::lower_bound(nodo->llaves_valores, nodo->llaves_valores + nodo->k, p.llave,
                                 [](const Par& a, int32_t llave) { return a.llave < llave; }) -
                nodo->llaves_valores);
            size_t hijo = static_cast<size_t>(nodo->hijos[i]);
            NodoB* u = &disco.leer(hijo);
            if (u->k == B_MAX) {
                NodoB izq, der;
                Par med;
                dividirNodoB(*u, izq, der, med);
                size_t ider = disco.agregar(der);   // U_der al final
                disco.escribir(hijo, izq);          // U_izq en el indice antiguo
                for (int j = nodo->k; j > i; --j) nodo->llaves_valores[j] = nodo->llaves_valores[j - 1];
                nodo->llaves_valores[i] = med;
                for (int j = nodo->k + 1; j > i + 1; --j) nodo->hijos[j] = nodo->hijos[j - 1];
                nodo->hijos[i + 1] = static_cast<int32_t>(ider);
                nodo->k++;
                disco.registrarEscritura();         // escritura de V
                idx = (p.llave <= med.llave) ? hijo : ider;
                nodo = &disco.leer(idx);
            } else {
                idx = hijo;
                nodo = u;
            }
        }

        // Hoja (no llena): insercion ordenada y escritura.
        int pos = static_cast<int>(
            std::upper_bound(nodo->llaves_valores, nodo->llaves_valores + nodo->k, p.llave,
                             [](int32_t llave, const Par& a) { return llave < a.llave; }) -
            nodo->llaves_valores);
        for (int j = nodo->k; j > pos; --j) nodo->llaves_valores[j] = nodo->llaves_valores[j - 1];
        nodo->llaves_valores[pos] = p;
        nodo->k++;
        disco.registrarEscritura();
    }

    // Guarda el arbol en un archivo binario (un nodo por bloque, secuencial).
    void guardar(const std::string& ruta) const { disco.guardar(ruta); }

    // Busqueda por rango [l, u] LEYENDO DESDE ARCHIVO.
    // Entrada: lector = archivo del arbol, idx = nodo actual (0 = raiz), l y u = rango,
    //          out = vector donde se agregan los pares encontrados (sin ordenar).
    static void buscarRango(LectorArchivo<NodoB>& lector, size_t idx, int32_t l, int32_t u,
                            std::vector<Par>& out) {
        NodoB nodo;
        lector.leer(idx, nodo);
        for (int i = 0; i < nodo.k; ++i)
            if (nodo.llaves_valores[i].llave >= l && nodo.llaves_valores[i].llave <= u)
                out.push_back(nodo.llaves_valores[i]);
        if (!nodo.es_interno) return;
        // hijos[i] guarda llaves en (kv[i-1], kv[i]]; se visita si ese intervalo toca [l, u]
        // (comparaciones inclusivas: seguras incluso con llaves repetidas).
        for (int i = 0; i <= nodo.k; ++i) {
            int32_t lo = (i == 0) ? INT_MIN : nodo.llaves_valores[i - 1].llave;
            int32_t hi = (i == nodo.k) ? INT_MAX : nodo.llaves_valores[i].llave;
            if (lo <= u && hi >= l) buscarRango(lector, static_cast<size_t>(nodo.hijos[i]), l, u, out);
        }
    }
};

#endif  // ARBOL_B_H
