// arbol_be.h
// Arbol B^epsilon en memoria externa. Nodos de 4096 bytes con c + lmax = 510.
//   epsilon ~ 0.25 -> ArbolBe<4, 506>
//   epsilon ~ 0.75 -> ArbolBe<126, 384>
#ifndef ARBOL_BE_H
#define ARBOL_BE_H

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

#include "disco.h"

// Nodo del arbol B^epsilon: 12 + 4c + 4(c+1) + 8*lmax = 8(2 + c + lmax) = 4096 bytes.
template <int C, int LMAX>
struct NodoBe {
    int32_t es_interno;     // 1 = interno, 0 = hoja
    int32_t k_llaves;       // llaves divisorias activas (0 en hojas)
    int32_t k_buffer;       // pares en buffer (0 <= k_buffer <= LMAX)
    int32_t llaves[C];      // llaves divisorias ordenadas (ignorado en hojas)
    int32_t hijos[C + 1];   // posiciones de los k_llaves + 1 hijos (ignorado en hojas)
    Par buffer[LMAX];       // interno: log de insertaciones pendientes; hoja: los datos. Ordenado.
};

template <int C, int LMAX>
class ArbolBe {
public:
    using Nodo = NodoBe<C, LMAX>;
    DiscoSimulado<Nodo> disco;  // arreglo de nodos en RAM; disco.leer(0) es la raiz

    // Crea un arbol vacio (raiz = hoja vacia en la posicion 0).
    // Entrada: alpha en (0,1), fraccion usada en el vaciado de buffer: se bajan los grupos
    //          de al menos ceil(f * alpha) elementos, con f = ceil(k_buffer / (k_llaves + 1)).
    explicit ArbolBe(double alpha = 0.5) : alpha_(alpha) {
        Nodo raiz{};
        disco.agregar(raiz);
    }

    // Inserta un par (entra siempre por la raiz).
    // Entrada: p = par llave-valor.
    void insertar(const Par& p) {
        Nodo* raiz = &disco.leer(0);

        // (a) Raiz hoja.
        if (!raiz->es_interno) {
            if (raiz->k_buffer < LMAX) {
                insertarOrdenado(*raiz, p);
                disco.registrarEscritura();
                return;
            }
            std::vector<Par> todos(raiz->buffer, raiz->buffer + raiz->k_buffer);
            Nodo izq{}, der{};
            int32_t km;
            dividirHoja(todos, izq, der, km);
            size_t iizq = disco.agregar(izq);
            size_t ider = disco.agregar(der);
            Nodo nueva{};
            nueva.es_interno = 1;
            nueva.k_llaves = 1;
            nueva.llaves[0] = km;
            nueva.hijos[0] = static_cast<int32_t>(iizq);
            nueva.hijos[1] = static_cast<int32_t>(ider);
            insertarOrdenado(nueva, p);
            disco.escribir(0, nueva);
            return;
        }

        // (b) Raiz interna: mientras el buffer este lleno, vaciar (o dividir la raiz si
        //     alcanzo c llaves).
        while (raiz->k_buffer == LMAX) {
            if (raiz->k_llaves >= C) {
                Nodo izq{}, der{};
                int32_t km;
                dividirInterno(*raiz, izq, der, km);
                size_t iizq = disco.agregar(izq);
                size_t ider = disco.agregar(der);
                Nodo nueva{};
                nueva.es_interno = 1;
                nueva.k_llaves = 1;
                nueva.llaves[0] = km;
                nueva.hijos[0] = static_cast<int32_t>(iizq);
                nueva.hijos[1] = static_cast<int32_t>(ider);
                insertarOrdenado(nueva, p);
                disco.escribir(0, nueva);
                return;
            }
            vaciar(*raiz);
        }
        insertarOrdenado(*raiz, p);
        disco.registrarEscritura();
    }

    // Guarda el arbol en un archivo binario (un nodo por bloque, secuencial).
    void guardar(const std::string& ruta) const { disco.guardar(ruta); }

    // Busqueda por rango [l, u] LEYENDO DESDE ARCHIVO. Examina el buffer de todos los
    // nodos visitados y baja a los hijos cuyos rangos intersectan [l, u].
    // Entrada: lector = archivo del arbol, idx = nodo actual (0 = raiz), l y u = rango,
    //          out = vector donde se agregan los pares encontrados (sin ordenar).
    static void buscarRango(LectorArchivo<Nodo>& lector, size_t idx, int32_t l, int32_t u,
                            std::vector<Par>& out) {
        Nodo nodo;
        lector.leer(idx, nodo);
        const Par* ini = nodo.buffer;
        const Par* fin = nodo.buffer + nodo.k_buffer;
        const Par* it = std::lower_bound(ini, fin, l,
                                         [](const Par& a, int32_t v) { return a.llave < v; });
        for (; it != fin && it->llave <= u; ++it) out.push_back(*it);
        if (!nodo.es_interno) return;
        for (int i = 0; i <= nodo.k_llaves; ++i) {
            int32_t lo = (i == 0) ? INT_MIN : nodo.llaves[i - 1];
            int32_t hi = (i == nodo.k_llaves) ? INT_MAX : nodo.llaves[i];
            if (lo <= u && hi >= l) buscarRango(lector, static_cast<size_t>(nodo.hijos[i]), l, u, out);
        }
    }

private:
    double alpha_;

    // Mezcla (in place, desde el final) los m pares ordenados "nuevos" en buf (k pares).
    // Requiere k + m <= LMAX. Actualiza k.
    static void mezclar(Par* buf, int32_t& k, const Par* nuevos, int m) {
        int i = k - 1, j = m - 1, w = k + m - 1;
        while (j >= 0) {
            if (i >= 0 && buf[i].llave > nuevos[j].llave) buf[w--] = buf[i--];
            else buf[w--] = nuevos[j--];
        }
        k += m;
    }

    // Inserta un par en el buffer ordenado del nodo n (debe haber espacio).
    static void insertarOrdenado(Nodo& n, const Par& p) { mezclar(n.buffer, n.k_buffer, &p, 1); }

    // Split de hoja: reparte "todos" (pares ordenados, n <= 2*LMAX) en dos hojas.
    // Salida: izq (primeros ceil(n/2) pares), der (el resto) y km = llave del ultimo par de izq
    //         (llave divisoria; el mediano queda en izq y sube como copia al padre).
    static void dividirHoja(const std::vector<Par>& todos, Nodo& izq, Nodo& der, int32_t& km) {
        int n = static_cast<int>(todos.size());
        int nl = (n + 1) / 2;
        izq = Nodo{};
        der = Nodo{};
        std::copy(todos.begin(), todos.begin() + nl, izq.buffer);
        std::copy(todos.begin() + nl, todos.end(), der.buffer);
        izq.k_buffer = nl;
        der.k_buffer = n - nl;
        km = todos[nl - 1].llave;
    }

    // Split de nodo interno con c llaves.
    // Entrada: u (k_llaves == C). Salida: izq (c/2 - 1 llaves, c/2 hijos), der (c/2 llaves,
    //          c/2 + 1 hijos), km = llave en la posicion c/2 (desde 1). El buffer se reparte:
    //          llaves <= km a izq, > km a der.
    static void dividirInterno(const Nodo& u, Nodo& izq, Nodo& der, int32_t& km) {
        const int m = C / 2;
        izq = Nodo{};
        der = Nodo{};
        izq.es_interno = der.es_interno = 1;
        km = u.llaves[m - 1];
        izq.k_llaves = m - 1;
        der.k_llaves = C - m;
        std::copy(u.llaves, u.llaves + (m - 1), izq.llaves);
        std::copy(u.llaves + m, u.llaves + C, der.llaves);
        std::copy(u.hijos, u.hijos + m, izq.hijos);
        std::copy(u.hijos + m, u.hijos + C + 1, der.hijos);
        const Par* fin = u.buffer + u.k_buffer;
        const Par* corte = std::upper_bound(u.buffer, fin, km,
                                            [](int32_t v, const Par& a) { return v < a.llave; });
        izq.k_buffer = static_cast<int32_t>(corte - u.buffer);
        der.k_buffer = static_cast<int32_t>(fin - corte);
        std::copy(u.buffer, corte, izq.buffer);
        std::copy(corte, fin, der.buffer);
    }

    // Inserta en el padre V (con k_llaves < C) la llave divisoria km en la posicion ci
    // y el nuevo hijo (indice ider) en la posicion ci + 1.
    static void insertarLlaveEnPadre(Nodo& v, int ci, int32_t km, int32_t ider) {
        for (int j = v.k_llaves; j > ci; --j) v.llaves[j] = v.llaves[j - 1];
        v.llaves[ci] = km;
        for (int j = v.k_llaves + 1; j > ci + 1; --j) v.hijos[j] = v.hijos[j - 1];
        v.hijos[ci + 1] = ider;
        v.k_llaves++;
    }

    // Vaciado de buffer (flush) del nodo interno V (ya leido).
    // Agrupa el buffer por hijo, y para los hijos con al menos ceil(f*alpha) elementos
    // (f = ceil(k_buffer/(k_llaves+1))) baja su grupo, de derecha a izquierda.
    // Si V ya tiene c llaves no se pueden dividir mas hijos: esos grupos se quedan en V.
    // Al terminar escribe V (1 escritura).
    void vaciar(Nodo& V) {
        const int n = V.k_buffer;
        if (n == 0) return;
        const int nc = V.k_llaves + 1;
        int ini[C + 1], fin[C + 1];
        int pos = 0;
        for (int i = 0; i < nc; ++i) {
            int e = n;
            if (i < V.k_llaves) {
                e = static_cast<int>(std::upper_bound(V.buffer + pos, V.buffer + n, V.llaves[i],
                                                      [](int32_t v, const Par& a) { return v < a.llave; }) -
                                     V.buffer);
            }
            ini[i] = pos;
            fin[i] = e;
            pos = e;
        }
        const int f = (n + nc - 1) / nc;
        const int umbral = std::max(1, static_cast<int>(std::ceil(f * alpha_)));
        for (int i = nc - 1; i >= 0; --i) {
            const int m = fin[i] - ini[i];
            if (m < umbral) continue;
            std::vector<Par> grupo(V.buffer + ini[i], V.buffer + fin[i]);
            std::copy(V.buffer + fin[i], V.buffer + V.k_buffer, V.buffer + ini[i]);
            V.k_buffer -= m;
            std::vector<Par> sobrante = entregar(V, i, std::move(grupo));
            if (!sobrante.empty()) mezclar(V.buffer, V.k_buffer, sobrante.data(), static_cast<int>(sobrante.size()));
        }
        disco.registrarEscritura();
    }

    // Entrega "grupo" (pares ordenados que corresponden al hijo ci de V) a ese hijo.
    //  - Hoja: si cabe, se mezcla; si no, split de hoja (la llave sube a V).
    //  - Interno: vacia su buffer hasta que quepa el grupo (dividiendolo antes si tiene c llaves).
    // Si V no tiene espacio para una llave divisoria nueva, devuelve los pares que no pudo entregar
    // (el llamador los deja en el buffer de V). Devuelve un vector vacio si todo fue entregado.
    std::vector<Par> entregar(Nodo& V, int ci, std::vector<Par> grupo) {
        const size_t hijo = static_cast<size_t>(V.hijos[ci]);
        Nodo* U = &disco.leer(hijo);
        auto cmp = [](const Par& a, const Par& b) { return a.llave < b.llave; };

        if (!U->es_interno) {
            const int m = static_cast<int>(grupo.size());
            if (U->k_buffer + m <= LMAX) {
                mezclar(U->buffer, U->k_buffer, grupo.data(), m);
                disco.registrarEscritura();
                return {};
            }
            if (V.k_llaves >= C) return grupo;
            std::vector<Par> todos(U->k_buffer + m);
            std::merge(U->buffer, U->buffer + U->k_buffer, grupo.begin(), grupo.end(), todos.begin(), cmp);
            Nodo izq{}, der{};
            int32_t km;
            dividirHoja(todos, izq, der, km);
            size_t ider = disco.agregar(der);   // U_der al final
            disco.escribir(hijo, izq);          // U_izq en el lugar de U
            insertarLlaveEnPadre(V, ci, km, static_cast<int32_t>(ider));
            return {};
        }

        while (true) {
            const int m = static_cast<int>(grupo.size());
            if (U->k_buffer + m <= LMAX) {
                mezclar(U->buffer, U->k_buffer, grupo.data(), m);
                disco.registrarEscritura();
                return {};
            }
            if (U->k_llaves >= C) {
                if (V.k_llaves >= C) return grupo;
                Nodo izq{}, der{};
                int32_t km;
                dividirInterno(*U, izq, der, km);
                size_t ider = disco.agregar(der);
                disco.escribir(hijo, izq);
                insertarLlaveEnPadre(V, ci, km, static_cast<int32_t>(ider));
                auto corte = std::upper_bound(grupo.begin(), grupo.end(), km,
                                              [](int32_t v, const Par& a) { return v < a.llave; });
                std::vector<Par> gl(grupo.begin(), corte), gr(corte, grupo.end());
                std::vector<Par> sobrante_r, sobrante_l;
                if (!gr.empty()) sobrante_r = entregar(V, ci + 1, std::move(gr));
                if (!gl.empty()) sobrante_l = entregar(V, ci, std::move(gl));
                sobrante_l.insert(sobrante_l.end(), sobrante_r.begin(), sobrante_r.end());
                return sobrante_l;
            }
            vaciar(*U);  // libera al menos un elemento (U tiene < c llaves)
        }
    }
};

#endif  // ARBOL_BE_H
