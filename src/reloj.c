/* Reloj (app de fábrica de senew): la hora y la fecha del RTC, ponerlo en hora con el
 * teclado, cronómetro y cuenta atrás, y una alarma diaria.
 * La ALARMA y el TEMPORIZADOR son SERVICIOS del sistema (los vigila el standby y suenan con la app cerrada): esta app solo
 * los programa (ap_ajuste_poner W5_AJ_ALARMA_*, ap_temporizador_poner). El CRONÓMETRO es de la app: en el firmware vivía en
 * estáticos que sobrevivían a salir; aquí se guarda en el fichero «estado» al salir (con la base en ap_ms(), que no se
 * reinicia al descargar la app) y se recupera al abrir, así que sigue «corriendo» con la app cerrada, como antes. Si el
 * teléfono se ha reiniciado entretanto (ap_ms() volvió a empezar), el cronómetro vuelve a cero: lo mismo que hacían los
 * estáticos del firmware al reiniciar. */
#include "w5.h"
#include "w5_pantalla.h"                  /* (1.7) la letra nueva */

W5_APP(.api_menor = W5_PANT_MENOR, .nombre = "Reloj", .icono_gen = W5_IC_RELOJ, .version = 1, .caps = W5_CAP_RELOJ | W5_CAP_GIRA, .monton = 1024);

static const char *const dia_n[7] = { "lun", "mar", "mié", "jue", "vie", "sáb", "dom" };
static const char *const mes_n[12] = { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" };
static int dia_semana(int a, int m, int d)                 /* 0 = lunes … 6 = domingo */
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 1 || m > 12) return 0;
    if (m < 3) a--;
    return ((a + a / 4 - a / 100 + a / 400 + t[m - 1] + d) % 7 + 6) % 7;
}
static int tecla_defecto(W5Ctx c, int k, void *dato) { (void)dato; return ui_teclas_defecto(c, k); }

/* ---- estado que sobrevive a salir de la app: el fichero «estado» ------------------------------------------------------ */
typedef struct {
    u32 base, acum; u32 vuelta[4]; u8 corre, n_vueltas, fecha_ok, res;      /* el cronómetro, en ms de ap_ms() */
    int min, seg; u32 total;                                                /* la cuenta atrás: lo elegido y su total (para la barra) */
    u32 guardado_ms; W5Fecha fecha;                                         /* cuándo se guardó (para saber si hubo reinicio) */
} Estado;
static Estado crono = { .min = 1 };
static Estado leido;
static u32 crono_ms(void) { return crono.acum + (crono.corre ? ap_ms() - crono.base : 0); }
static char *fmt_crono(u32 ms) { return ui_fmt("%02d:%02d.%d", ms / 60000, ms / 1000 % 60, ms / 100 % 10); }
static int dias(int a, int m, int d)                       /* días desde el 1-1-2000 (para comparar dos fechas) */
{
    static const int acum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int y = a - 2000, n = y * 365 + (y + 3) / 4 + acum[(m - 1) % 12] + d - 1;
    if (m > 2 && a % 4 == 0) n++;
    return n;
}
static void cargar(void)
{
    Estado e;
    W5Fecha f;
    leido = crono;                                         /* sin fichero: lo de fábrica (así no se escribe si nada cambia) */
    if (ap_fich_leer("estado", 0, &e, sizeof e) != (int)sizeof e) return;
    leido = e;
    if (e.corre || e.acum) {
        int reinicio = ap_ms() < e.guardado_ms;
        if (!reinicio && e.fecha_ok && ap_fecha(&f) == W5_OK) {   /* el reloj de pared tampoco cuadra: ap_ms() dio la vuelta a otro arranque */
            int pared = (dias(f.anio, f.mes, f.dia) - dias(e.fecha.anio, e.fecha.mes, e.fecha.dia)) * 86400 +
                        (f.hora - e.fecha.hora) * 3600 + (f.min - e.fecha.min) * 60 + (f.seg - e.fecha.seg);
            int dif = pared - (int)((ap_ms() - e.guardado_ms) / 1000), r = dif % 900;
            if (r < 0) r += 900;
            if ((dif > 120 || dif < -120) && r > 120 && r < 780) reinicio = 1;   /* (un cambio de zona mueve múltiplos de 15 min: eso no cuenta) */
        }
        if (reinicio) { e.corre = 0; e.acum = 0; e.n_vueltas = 0; }
    }
    if (e.n_vueltas > 4) e.n_vueltas = 4;
    if (e.min < 0 || e.min > 99) e.min = 1;
    if (e.seg < 0 || e.seg > 59) e.seg = 0;
    crono = e;
}
static void guardar(void)
{
    crono.guardado_ms = ap_ms();
    crono.fecha_ok = ap_fecha(&crono.fecha) == W5_OK;
    if (!(crono.corre || crono.acum) && crono.min == leido.min && crono.seg == leido.seg && crono.total == leido.total &&
        !(leido.corre || leido.acum)) return;              /* nada que conservar que no esté ya: no gasta flash */
    ap_fich_escribir("estado", &crono, sizeof crono);
}

/* ---- poner en hora ------------------------------------------------------------------------------------------------------ */
static struct { int h, mi, d, me, a; } ph;
static void ph_iniciar(W5Ctx c, void *dato)
{
    W5Fecha f;
    (void)c; (void)dato;
    ap_fecha(&f);
    ph.h = f.hora; ph.mi = f.min; ph.d = f.dia; ph.me = f.mes; ph.a = f.anio;
    if (ph.a < 2000 || ph.a > 2099) { ph.a = 2026; ph.me = 1; ph.d = 1; }
}
static void ph_cambio(W5Ctx c, void *d) { (void)d; ui_sucio(ui_mi_pantalla(c)); }
static int ph_tecla(W5Ctx t, int k, void *dato)
{
    (void)t; (void)dato;
    if (k == W5K_SEL_IZQ || k == W5K_OK) {
        W5Fecha f = { (unsigned short)ph.a, (unsigned char)ph.me, (unsigned char)ph.d, (unsigned char)ph.h, (unsigned char)ph.mi, 0, 0 };
        if (ap_hora_poner(&f) != W5_OK) { ui_aviso("Fecha no válida", ui_fmt("%s no tiene día %d.", mes_n[(ph.me + 11) % 12], ph.d)); return 1; }
        ui_sacar();
        ui_aviso("Reloj", ui_fmt("Puesto a las %02d:%02d del %d de %s de %d (hora local, %s).", ph.h, ph.mi, ph.d, mes_n[(ph.me + 11) % 12], ph.a, ap_zona_texto()));
        return 1;
    }
    if (k == W5K_SEL_DER) { ui_sacar(); return 1; }
    return 0;
}
static W5W ph_construir(W5Ctx c, void *dato)
{
    unsigned tenue = ui_color(W5_COL_TENUE);
    (void)c; (void)dato;
    return Teclas(ph_tecla, ui_andamio("Poner en hora", Columna(A_PRINCIPAL(W5P_CENTRO), A_CRUZADO(W5P_CENTRO), A_ESPACIO(6), HIJOS(
        Texto("hora : minuto", A_COLOR(tenue), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA))),
        Fila(A_ESPACIO(4), HIJOS(Selector(&ph.h, A_MAX(23), A_AUTOFOCO(1)), Texto(":", A_ESCALA(2)), Selector(&ph.mi, A_MAX(59)))),
        Texto("día / mes / año", A_COLOR(tenue), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA))),
        Fila(A_ESPACIO(4), HIJOS(Selector(&ph.d, A_MIN(1), A_MAX(31), A_AL_CAMBIAR(ph_cambio)), Texto("/", A_ESCALA(2)), Selector(&ph.me, A_MIN(1), A_MAX(12), A_AL_CAMBIAR(ph_cambio)),
                                 Texto("/", A_ESCALA(2)), Selector(&ph.a, A_MIN(2000), A_MAX(2099), A_DIGITOS(4), A_AL_CAMBIAR(ph_cambio)))),
        Texto(ui_fmt("%s %d de %s", dia_n[dia_semana(ph.a, ph.me, ph.d)], ph.d, mes_n[(ph.me + 11) % 12]), A_COLOR(ui_color(W5_COL_ACENTO))),
        Parrafo("Izq./der.: campo. Arriba/abajo: valor. También se teclea el número.", A_COLOR(tenue), A_CENTRADO(1), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA))))),
        "Guardar", "Cancelar"));
}

/* ---- cronómetro ------------------------------------------------------------------------------------------------------------ */
static void crono_tic(W5Ctx c) { if (crono.corre) ui_sucio(c); }
static void crono_iniciar(W5Ctx c, void *dato) { (void)dato; ui_cada(c, 100, crono_tic); }
static int crono_tecla(W5Ctx t, int k, void *dato)
{
    (void)dato;
    if (k == W5K_OK) {
        if (crono.corre) { crono.acum = crono_ms(); crono.corre = 0; }
        else { crono.base = ap_ms(); crono.corre = 1; }
    } else if (k == W5K_SEL_IZQ) {
        if (crono.corre) { if (crono.n_vueltas < 4) crono.vuelta[crono.n_vueltas++] = crono_ms(); }
        else { crono.acum = 0; crono.n_vueltas = 0; }
    } else return ui_teclas_defecto(t, k);
    ui_sucio(ui_mi_pantalla(t));
    return 1;
}
static W5W crono_construir(W5Ctx c, void *dato)
{
    W5W v[8]; int i, n = 0;
    (void)c; (void)dato;
    v[n++] = Texto(fmt_crono(crono_ms()), A_ESCALA(4), A_COLOR(ui_color(crono.corre ? W5_COL_ACENTO : W5_COL_TEXTO)));
    v[n++] = Texto(crono.corre ? "OK: parar" : crono.acum ? "OK: seguir" : "OK: empezar", A_COLOR(ui_color(W5_COL_TENUE)), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA)));
    for (i = 0; i < crono.n_vueltas; i++) v[n++] = Texto(ui_fmt("vuelta %d   %s", i + 1, fmt_crono(crono.vuelta[i])));
    return Teclas(crono_tecla, ui_andamio("Cronómetro", Columna(A_PRINCIPAL(W5P_CENTRO), A_CRUZADO(W5P_CENTRO), A_ESPACIO(8), W5A_HIJOS, (W5Pal)ui_lista(v, (unsigned)n)),
        crono.corre ? "Vuelta" : "A cero", "Atrás"));
}

/* ---- cuenta atrás (el temporizador del SISTEMA) ------------------------------------------------------------------------------ */
static void cuenta_tic(W5Ctx c) { ui_sucio(c); }
static void cuenta_iniciar(W5Ctx c, void *dato) { (void)dato; ui_cada(c, 200, cuenta_tic); }
static int cuenta_tecla(W5Ctx t, int k, void *dato)
{
    int corre = ap_temporizador_queda() >= 0;
    (void)dato;
    if (k == W5K_SEL_IZQ || (k == W5K_OK && corre)) {
        if (corre) ap_temporizador_poner(0);
        else if (crono.min || crono.seg) {
            crono.total = (u32)(crono.min * 60 + crono.seg) * 1000;
            ap_temporizador_poner(crono.total);
        }
        ui_sucio(ui_mi_pantalla(t));
        return 1;
    }
    return ui_teclas_defecto(t, k);
}
static W5W cuenta_construir(W5Ctx c, void *dato)
{
    int q = ap_temporizador_queda(), corre = q >= 0;
    u32 queda = corre ? (u32)q : 0, total = crono.total > queda ? crono.total : queda;
    unsigned tenue = ui_color(W5_COL_TENUE);
    (void)c; (void)dato;
    return Teclas(cuenta_tecla, ui_andamio("Temporizador", Columna(A_PRINCIPAL(W5P_CENTRO), A_CRUZADO(W5P_CENTRO), A_ESPACIO(10), HIJOS(
        corre ? Texto(ui_fmt("%02d:%02d", (queda + 999) / 60000, (queda + 999) / 1000 % 60), A_ESCALA(4), A_COLOR(ui_color(W5_COL_ACENTO)))
              : Fila(A_ESPACIO(4), HIJOS(Selector(&crono.min, A_MAX(99), A_AUTOFOCO(1)), Texto(":", A_ESCALA(2)), Selector(&crono.seg, A_MAX(59)))),
        corre ? RellenoXY(16, 0, Progreso((int)(total - queda), (int)total)) : Texto("minutos : segundos", A_COLOR(tenue), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA))),
        Parrafo(corre ? "Avisará aunque salgas de esta pantalla." : "Arriba/abajo o teclea el número. La tecla izquierda lo pone en marcha.", A_COLOR(tenue), A_CENTRADO(1), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA))))),
        corre ? "Parar" : "Empezar", "Atrás"));
}

/* ---- alarma (la del SISTEMA: la vigila el standby) --------------------------------------------------------------------------- */
static struct { int h, m; } al;
static void alarma_iniciar(W5Ctx c, void *dato) { (void)c; (void)dato; al.h = ap_ajuste(W5_AJ_ALARMA_H); al.m = ap_ajuste(W5_AJ_ALARMA_M); }
static void alarma_cambio(W5Ctx c, void *d) { (void)c; (void)d; ap_ajuste_poner(W5_AJ_ALARMA_H, al.h, 0); ap_ajuste_poner(W5_AJ_ALARMA_M, al.m, 0); }
static void alarma_desechar(W5Ctx c, void *dato) { (void)c; (void)dato; ap_ajuste_poner(W5_AJ_ALARMA_M, al.m, 1); }   /* al salir: la hora elegida queda guardada */
static int alarma_tecla(W5Ctx t, int k, void *dato)
{
    (void)dato;
    if (k == W5K_SEL_IZQ || k == W5K_OK) { ap_ajuste_poner(W5_AJ_ALARMA_ON, !ap_ajuste(W5_AJ_ALARMA_ON), 1); ui_sucio(ui_mi_pantalla(t)); return 1; }
    return ui_teclas_defecto(t, k);
}
static W5W alarma_construir(W5Ctx c, void *dato)
{
    int on = ap_ajuste(W5_AJ_ALARMA_ON) == 1;
    unsigned tenue = ui_color(W5_COL_TENUE);
    (void)c; (void)dato;
    return Teclas(alarma_tecla, ui_andamio("Alarma", Columna(A_PRINCIPAL(W5P_CENTRO), A_CRUZADO(W5P_CENTRO), A_ESPACIO(6), HIJOS(
        Fila(A_ESPACIO(4), HIJOS(Selector(&al.h, A_MAX(23), A_AUTOFOCO(1), A_AL_CAMBIAR(alarma_cambio)), Texto(":", A_ESCALA(2)),
                                 Selector(&al.m, A_MAX(59), A_AL_CAMBIAR(alarma_cambio)))),
        Texto(on ? "ACTIVADA" : "desactivada", A_ESCALA(2), A_COLOR(on ? ui_color(W5_COL_ACENTO) : tenue)),
        Parrafo("Diaria; se guarda en el teléfono. La dispara la alarma del propio RTC (IRQ 43) y, por si acaso, una comparación cada segundo. Avisa en pantalla y suena por el zumbador hasta que pulses una tecla (según el volumen de Ajustes).", A_COLOR(tenue), A_CENTRADO(1), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA))))),
        on ? "Desactivar" : "Activar", "Atrás"));
}

/* ---- la pantalla principal del reloj ------------------------------------------------------------------------------------------ */
static struct { W5Fecha f; int valida, sel; } rl;
static void reloj_tic(W5Ctx c) { rl.valida = ap_fecha(&rl.f) == W5_OK; ui_sucio(c); }
static void reloj_iniciar(W5Ctx c, void *dato) { (void)dato; rl.valida = ap_fecha(&rl.f) == W5_OK; ui_cada(c, 250, reloj_tic); }
static void reloj_abrir(W5Ctx c, int i, void *dato)
{
    (void)c; (void)dato;
    if (i == 0) ui_empujar(ui_pantalla(ph_construir, ph_iniciar, NULL, NULL));
    else if (i == 1) ui_empujar(ui_pantalla(crono_construir, crono_iniciar, NULL, NULL));
    else if (i == 2) ui_empujar(ui_pantalla(cuenta_construir, cuenta_iniciar, NULL, NULL));
    else ui_empujar(ui_pantalla(alarma_construir, alarma_iniciar, alarma_desechar, NULL));
}
static W5W reloj_fila(W5Ctx c, int i, int sel, void *dato)
{
    int q = ap_temporizador_queda();
    u32 queda = q > 0 ? (u32)q : 0;
    (void)c; (void)dato;
    switch (i) {
    case 0: return ui_fila("Poner en hora", NULL, sel);
    case 1: return ui_fila("Cronómetro", crono.corre || crono.acum ? fmt_crono(crono_ms()) : NULL, sel);
    case 2: return ui_fila("Temporizador", q >= 0 ? ui_fmt("%02d:%02d", (queda + 999) / 60000, (queda + 999) / 1000 % 60) : NULL, sel);
    default: return ui_fila("Alarma", ap_ajuste(W5_AJ_ALARMA_ON) == 1 ? ui_fmt("%02d:%02d", ap_ajuste(W5_AJ_ALARMA_H), ap_ajuste(W5_AJ_ALARMA_M)) : "no", sel);
    }
}
static W5W reloj_construir(W5Ctx c, void *dato)
{
    int h12 = ap_ajuste(W5_AJ_HORA12) == 1;
    W5Fecha *f = &rl.f;
    (void)c; (void)dato;
    rl.valida = ap_fecha(&rl.f) == W5_OK;                  /* al construir, la de ahora: bajo un aviso suyo la app no recibe tics */
    return Teclas(tecla_defecto, ui_andamio("Reloj", Columna(A_CRUZADO(W5P_ESTIRAR), HIJOS(
        RellenoXY(0, 10, Columna(A_CRUZADO(W5P_CENTRO), A_ESPACIO(6), HIJOS(
            Texto(ap_hora_texto(1), A_ESCALA(h12 ? 2 : 3), A_MARCAS(h12 ? 0 : W5_F_BASTA)),   /* (MD) a escala 3 = display: en material3, Roboto de 30 px */
            Texto(ui_fmt("%s %d de %s de %d", dia_n[dia_semana(f->anio, f->mes, f->dia)], f->dia, mes_n[(f->mes + 11) % 12], f->anio), A_COLOR(ui_color(W5_COL_ACENTO)), A_MARCAS(W5_F_ESTILO(W5_T_TITULO))),   /* (MD) estilo título */
            Texto(rl.valida ? ui_fmt("RTC del chip, %s", ap_zona_texto()) : "el RTC dice que la hora NO es válida", A_COLOR(ui_color(W5_COL_TENUE)), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA)))))),
        Expandido(ListaPerezosa(4, reloj_fila, A_AL_ACTIVAR(reloj_abrir), A_SEL(&rl.sel))))),
        "Abrir", "Atrás"));
}
W5Pal app_suceso(int suceso, W5Pal a, W5Pal b)
{
    (void)a; (void)b;
    if (suceso == W5_ABRIR) { cargar(); return (W5Pal)ui_pantalla(reloj_construir, reloj_iniciar, NULL, NULL); }
    if (suceso == W5_SALIR) guardar();
    return 0;
}
