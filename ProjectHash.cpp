#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdint.h>     /* Для использования явных 32-битных типов */


/* ХЕШ-ФУНКЦИЯ FNV-1a (32-битная версия)
 *
 * Параметры FNV-1a 32-bit:
 *   - offset_basis = 2166136261 (0x811C9DC5) — начальное значение
 *   - FNV_prime = 16777619 (0x01000193) — простое число для перемешивания
 *
 * Почему именно 32-битная версия:
 *   - Достаточна для большинства приложений
 *   - Быстрее 64-битной на 32-битных системах
 *   - Проще в реализации
 *   - Даёт хорошее распределение для таблиц до 2^20 элементов
*/
uint32_t fnv1a_hash(const char* str, uint32_t table_size) {
    /*
     * FNV-1a алгоритм:
     * 1. Инициализируем хеш константой offset_basis (2166136261)
     * 2. Для каждого байта строки:
     *    a) XOR с текущим байтом (hash ^= byte)
     *    b) Умножение на FNV_prime (hash *= 16777619)
     * 3. Возвращаем hash % table_size (приведение к диапазону таблицы)
     * умножение выполняется по модулю 2^32 благодаря
     * использованию 32-битного беззнакового типа uint32_t.
     */
    uint32_t hash = 2166136261U;   /* offset_basis для FNV-1a 32-bit */

    /*
     * Проходим по каждому байту строки (включая завершающий)
     * Используем unsigned char для корректной работы с UTF-8 и бинарными данными
     */
    const unsigned char* ptr = (const unsigned char*)str;

    while (*ptr) {
        /*
         * Шаг 1: XOR текущего байта с хешем
         * Это главное отличие FNV-1a от FNV-1 (в FNV-1 сначала умножение,
         * потом XOR). FNV-1a даёт лучшее распределение для коротких строк.
         */
        hash ^= (uint32_t)*ptr;

        /*
         * Шаг 2: Умножение на FNV_prime (16777619)
         * 16777619 = 0x01000193 — выбрано как простое число с хорошими
         * свойствами для перемешивания бит.
         * Переполнение происходит автоматически благодаря uint32_t
         * (умножение по модулю 2^32).
         */
        hash *= 16777619U;   /* FNV_prime для 32-bit */

        ptr++;  /* Переход к следующему байту */
    }

    /*
     * Приводим хеш к размеру таблицы.
     * Используем остаток от деления (самый простой способ).
    */
    return hash % table_size;
}

/* БАЗОВАЯ ХЕШ-ТАБЛИЦА (с цепочками)
 * Структура данных, хранящая пары (ключ, значение).
 * Использует метод цепочек для разрешения коллизий.
*/

/* Узел цепочки — хранит ключ и значение */
typedef struct HashNode {
    char* key;              /* Ключ (строка) */
    int value;              /* Значение (для множества: 1, для мультимножества: кратность) */
    struct HashNode* next;  /* Указатель на следующий узел в цепочке */
} HashNode;

/* Структура хеш-таблицы */
typedef struct {
    HashNode** buckets;     /* Массив указателей на начало цепочек */
    uint32_t size;          /* Размер таблицы */
    uint32_t count;         /* Количество уникальных ключей */
    uint32_t(*hash_func)(const char*, uint32_t); /* Указатель на хеш-функцию */
} HashTable;

/* СОЗДАНИЕ ХЕШ-ТАБЛИЦЫ
 * Параметры:
 *   - size: начальный размер таблицы
 *   - hash_func: указатель на хеш-функцию (если NULL, используется FNV-1a)
 * Возвращает: указатель на созданную таблицу или NULL при ошибке
*/
HashTable* ht_create(uint32_t size, uint32_t(*hash_func)(const char*, uint32_t)) {
    /*
     * Алгоритм создания:
     * 1. Выделяем память под структуру HashTable
     * 2. Инициализируем поля: размер, счётчик, хеш-функция
     * 3. Выделяем память под массив
     * 4. Обнуляем все указатели в массиве
     */

    HashTable* ht = (HashTable*)malloc(sizeof(HashTable));
    if (!ht) return NULL;  /* Проверка выделения памяти */

    ht->size = size;
    ht->count = 0;
    ht->hash_func = hash_func ? hash_func : fnv1a_hash;  /* По умолчанию FNV-1a */

    /* Выделяем память под массив указателей*/
    ht->buckets = (HashNode**)calloc(size, sizeof(HashNode*));
    if (!ht->buckets) {
        free(ht);
        return NULL;
    }

    return ht;
}

/* ПОИСК УЗЛА ПО КЛЮЧУ
 * Возвращает указатель на найденный узел.
 * Если prev_out не NULL, возвращает указатель на предыдущий узел (для удаления).
*/
HashNode* ht_find_node(HashTable* ht, const char* key, HashNode** prev_out) {
    /*
     * Алгоритм поиска:
     * 1. Вычисляем хеш ключа (индекс)
     * 2. Проходим по цепочке в этом контейнере
     * 3. Сравниваем строки (strcmp)
     * 4. Возвращаем найденный узел или NULL
     */

    uint32_t index = ht->hash_func(key, ht->size);
    HashNode* curr = ht->buckets[index];
    HashNode* prev = NULL;

    /* Обход цепочки */
    while (curr) {
        /* Сравнение ключей (строк) */
        if (strcmp(curr->key, key) == 0) {
            /* Ключ найден, возвращаем узел */
            if (prev_out) *prev_out = prev;
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }

    /* Ключ не найден */
    if (prev_out) *prev_out = prev;
    return NULL;
}

/* ВСТАВКА/ОБНОВЛЕНИЕ ЗНАЧЕНИЯ
 * Если ключ существует, обновляет значение.
 * Если ключа нет, создаёт новый узел и вставляет в начало цепочки.
 * Возвращает: true при успехе, false при ошибке.
*/
bool ht_insert(HashTable* ht, const char* key, int value) {
    uint32_t index = ht->hash_func(key, ht->size);
    HashNode* node = ht_find_node(ht, key, NULL);

    /*
     * Если ключ уже существует — обновляем значение
     * Это позволяет использовать таблицу как массив
     */
    if (node) {
        node->value = value;
        return true;
    }

    /*
     * Ключ не найден — создаём новый узел
     * Вставляем в начало цепочки
     */
    node = (HashNode*)malloc(sizeof(HashNode));
    if (!node) return false;

    /* Копируем ключ в память */
    node->key = (char*)malloc(strlen(key) + 1);
    if (!node->key) {
        free(node);
        return false;
    }
    strcpy_s(node->key, strlen(key) + 1, key);

    /* Устанавливаем значение */
    node->value = value;

    /* Вставка в начало цепочки */
    node->next = ht->buckets[index];
    ht->buckets[index] = node;
    ht->count++;  /* Увеличиваем счётчик уникальных ключей */

    return true;
}

/* ПОЛУЧЕНИЕ ЗНАЧЕНИЯ ПО КЛЮЧУ
 * Возвращает: true если ключ найден, иначе false.
 * Значение записывается в out_value (если указатель не NULL).
*/
bool ht_get(HashTable* ht, const char* key, int* out_value) {
    HashNode* node = ht_find_node(ht, key, NULL);
    if (!node) return false;  /* Ключ не найден */

    if (out_value) *out_value = node->value;
    return true;
}

/* УДАЛЕНИЕ КЛЮЧА
 * Удаляет узел из цепочки и освобождает память.
 * Возвращает: true если ключ найден и удалён, иначе false.
*/
bool ht_delete(HashTable* ht, const char* key) {
    /*
     * Алгоритм удаления:
     * 1. Находим узел и предыдущий узел
     * 2. Корректируем указатели (предыдущий → следующий)
     * 3. Освобождаем память
     * 4. Уменьшаем счётчик
     */

    uint32_t index = ht->hash_func(key, ht->size);
    HashNode* prev = NULL;
    HashNode* node = ht_find_node(ht, key, &prev);

    if (!node) return false;  /* Ключ не найден */

    /* Корректировка указателей */
    if (prev) {
        /* Узел в середине или конце цепочки */
        prev->next = node->next;
    }
    else {
        /* Узел в начале цепочки (голова) */
        ht->buckets[index] = node->next;
    }

    /* Освобождение памяти */
    free(node->key);
    free(node);
    ht->count--;  /* Уменьшаем счётчик уникальных ключей */

    return true;
}

/* ПРОВЕРКА НАЛИЧИЯ КЛЮЧА */
bool ht_contains(HashTable* ht, const char* key) {
    return ht_find_node(ht, key, NULL) != NULL;
}

/* ОСВОБОЖДЕНИЕ ПАМЯТИ */
void ht_destroy(HashTable* ht) {
    if (!ht) return;

    /*
     * Освобождаем все цепочки:
     * 1. Проходим по всем ячейкам
     * 2. Для каждой ячейки проходим по цепочке
     * 3. Освобождаем каждый узел и его ключ
     */
    for (uint32_t i = 0; i < ht->size; i++) {
        HashNode* curr = ht->buckets[i];
        while (curr) {
            HashNode* next = curr->next;
            free(curr->key);
            free(curr);
            curr = next;
        }
    }

    free(ht->buckets);
    free(ht);
}

/* ВЫВОД СОДЕРЖИМОГО */
void ht_print(HashTable* ht) {
    printf("HashTable (size=%u, count=%u):\n", ht->size, ht->count);
    for (uint32_t i = 0; i < ht->size; i++) {
        if (ht->buckets[i]) {
            printf("  [%u] ", i);
            HashNode* curr = ht->buckets[i];
            while (curr) {
                printf("'%s':%d ", curr->key, curr->value);
                curr = curr->next;
            }
            printf("\n");
        }
    }
}

/* МНОЖЕСТВО (Set)
 Множество — это хеш-таблица, где значение всегда 1 (присутствие).
 Добавлены операции: объединение, пересечение, разность, проверка подмножества.
 Теория множеств:
 * - Уникальные элементы (нет дубликатов)
 * - Операции: добавление, удаление, поиск, объединение (∪), пересечение (∩)
*/

/* Множество — обёртка над хеш-таблицей */
typedef struct {
    HashTable* ht;
} Set;

/* СОЗДАНИЕ МНОЖЕСТВА */
Set* set_create(uint32_t size) {
    Set* s = (Set*)malloc(sizeof(Set));
    if (!s) return NULL;

    /* Используем хеш-таблицу с FNV-1a */
    s->ht = ht_create(size, fnv1a_hash);
    if (!s->ht) {
        free(s);
        return NULL;
    }
    return s;
}

/* ДОБАВЛЕНИЕ ЭЛЕМЕНТА В МНОЖЕСТВО
 Если элемент уже есть, ничего не меняется.
 Возвращает: true при успехе, false при ошибке.
*/
bool set_add(Set* s, const char* key) {
    /* Множество хранит только присутствие, значение всегда 1 */
    return ht_insert(s->ht, key, 1);
}

/* УДАЛЕНИЕ ЭЛЕМЕНТА ИЗ МНОЖЕСТВА */
bool set_remove(Set* s, const char* key) {
    return ht_delete(s->ht, key);
}

/* ПРОВЕРКА ПРИНАДЛЕЖНОСТИ К МНОЖЕСТВУ */
bool set_contains(Set* s, const char* key) {
    return ht_contains(s->ht, key);
}

/* ПОЛУЧЕНИЕ РАЗМЕРА МНОЖЕСТВА */
uint32_t set_size(Set* s) {
    return s->ht->count;
}

/* ОБЪЕДИНЕНИЕ МНОЖЕСТВ: s = s ∪ other
 * Добавляет все элементы из other в s.
 * Если элемент уже есть в s, он остаётся (уникальность сохраняется).
*/
void set_union(Set* s, Set* other) {
    /* Алгоритм объединения:
     * Проходим по всем ячейкам other и добавляем все ключи в s
     */
    for (uint32_t i = 0; i < other->ht->size; i++) {
        HashNode* curr = other->ht->buckets[i];
        while (curr) {
            set_add(s, curr->key);  /* если уже есть, не изменится */
            curr = curr->next;
        }
    }
}

/* ОСВОБОЖДЕНИЕ МНОЖЕСТВА */
void set_destroy(Set* s) {
    if (!s) return;
    ht_destroy(s->ht);
    free(s);
}

/* ПЕРЕСЕЧЕНИЕ МНОЖЕСТВ: s = s ∩ other
 * Оставляет в s только те элементы, которые есть в other.
*/
void set_intersection(Set* s, Set* other) {
    /*
     * Алгоритм пересечения:
     * 1. Находим все ключи из s, которых нет в other
     * 2. Удаляем их из s (оставляем только общие)
     */

     /* Временное множество для хранения ключей, которые нужно удалить */
    Set* to_remove = set_create(s->ht->size);
    if (!to_remove) return;

    /* Проходим по всем элементам s */
    for (uint32_t i = 0; i < s->ht->size; i++) {
        HashNode* curr = s->ht->buckets[i];
        while (curr) {
            /* Если элемента нет в other — отмечаем для удаления */
            if (!set_contains(other, curr->key)) {
                set_add(to_remove, curr->key);
            }
            curr = curr->next;
        }
    }

    /* Удаляем все отмеченные ключи из s */
    for (uint32_t i = 0; i < to_remove->ht->size; i++) {
        HashNode* curr = to_remove->ht->buckets[i];
        while (curr) {
            set_remove(s, curr->key);
            curr = curr->next;
        }
    }

    set_destroy(to_remove);
}

/* РАЗНОСТЬ МНОЖЕСТВ: s = s \ other
 * Удаляет из s все элементы, которые есть в other.
 */
void set_difference(Set* s, Set* other) {
    /*
     * Алгоритм разности:
     * Проходим по всем элементам other и удаляем их из s
     */
    for (uint32_t i = 0; i < other->ht->size; i++) {
        HashNode* curr = other->ht->buckets[i];
        while (curr) {
            set_remove(s, curr->key);
            curr = curr->next;
        }
    }
}

/* ПРОВЕРКА ПОДМНОЖЕСТВА: other ⊆ s ?
 * Возвращает: true если каждый элемент other есть в s, иначе false.
*/
bool set_is_subset(Set* s, Set* other) {
    /*
     * Алгоритм проверки подмножества:
     * Проверяем, что каждый элемент other присутствует в s
     */
    for (uint32_t i = 0; i < other->ht->size; i++) {
        HashNode* curr = other->ht->buckets[i];
        while (curr) {
            if (!set_contains(s, curr->key)) return false;
            curr = curr->next;
        }
    }
    return true;
}

/* ПЕЧАТЬ МНОЖЕСТВА */
void set_print(Set* s) {
    printf("Set { ");
    bool first = true;
    for (uint32_t i = 0; i < s->ht->size; i++) {
        HashNode* curr = s->ht->buckets[i];
        while (curr) {
            if (!first) printf(", ");
            printf("%s", curr->key);
            first = false;
            curr = curr->next;
        }
    }
    printf(" } (size=%u)\n", set_size(s));
}

/* МУЛЬТИМНОЖЕСТВО (Multiset / Bag)
 * Мультимножество — обобщение множества, где элементы могут повторяться.
 * Каждый элемент имеет кратность (счётчик).
 * Теория мультимножеств:
 * - Элементы могут повторяться (кратность > 1)
 * - Операции: добавление, удаление одного/всех, подсчёт кратности
 * - Суммарный размер = сумма кратностей всех элементов
 * - Уникальных элементов = количество различных ключей
*/

/* Мультимножество — обёртка над хеш-таблицей */
typedef struct {
    HashTable* ht;
} Multiset;

/* СОЗДАНИЕ МУЛЬТИМНОЖЕСТВА */
Multiset* multiset_create(uint32_t size) {
    Multiset* ms = (Multiset*)malloc(sizeof(Multiset));
    if (!ms) return NULL;

    ms->ht = ht_create(size, fnv1a_hash);
    if (!ms->ht) {
        free(ms);
        return NULL;
    }
    return ms;
}

/*  ДОБАВЛЕНИЕ ОДНОГО ЭЛЕМЕНТА
 * Увеличивает кратность элемента на 1.
 * Если элемента нет, создаёт с кратностью 1.
*/
void multiset_add(Multiset* ms, const char* key) {
    int count;
    if (ht_get(ms->ht, key, &count)) {
        /* Элемент уже есть — увеличиваем кратность */
        ht_insert(ms->ht, key, count + 1);
    }
    else {
        /* Новый элемент — кратность 1 */
        ht_insert(ms->ht, key, 1);
    }
}

/* ДОБАВЛЕНИЕ N КОПИЙ ЭЛЕМЕНТА */
void multiset_add_n(Multiset* ms, const char* key, int n) {
    if (n <= 0) return;  /* Защита от отрицательного количества */

    int count;
    if (ht_get(ms->ht, key, &count)) {
        ht_insert(ms->ht, key, count + n);
    }
    else {
        ht_insert(ms->ht, key, n);
    }
}

/* УДАЛЕНИЕ ОДНОГО ЭКЗЕМПЛЯРА
 * Уменьшает кратность на 1.
 * Если кратность становится 0, элемент удаляется полностью.
 * Возвращает: true если элемент был удалён (кратность > 0), иначе false.
*/
bool multiset_remove_one(Multiset* ms, const char* key) {
    int count;
    if (!ht_get(ms->ht, key, &count)) return false;

    if (count > 1) {
        /* Кратность больше 1 — просто уменьшаем */
        ht_insert(ms->ht, key, count - 1);
    }
    else {
        /* Кратность = 1 — удаляем элемент полностью */
        ht_delete(ms->ht, key);
    }
    return true;
}

/* УДАЛЕНИЕ ВСЕХ ЭКЗЕМПЛЯРОВ
 * Полностью удаляет элемент из мультимножества.
 * Возвращает: true если элемент был удалён, иначе false.
*/
bool multiset_remove_all(Multiset* ms, const char* key) {
    return ht_delete(ms->ht, key);
}

/*  ПОЛУЧЕНИЕ КРАТНОСТИ ЭЛЕМЕНТА
 * Возвращает: кратность элемента (0 если элемента нет).
*/
int multiset_count(Multiset* ms, const char* key) {
    int count;
    if (ht_get(ms->ht, key, &count)) return count;
    return 0;  /* Элемент отсутствует */
}

/* ОБЩЕЕ КОЛИЧЕСТВО ЭЛЕМЕНТОВ (с учётом кратности) */
uint32_t multiset_total_size(Multiset* ms) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < ms->ht->size; i++) {
        HashNode* curr = ms->ht->buckets[i];
        while (curr) {
            total += curr->value;  /* Суммируем кратности */
            curr = curr->next;
        }
    }
    return total;
}

/* КОЛИЧЕСТВО УНИКАЛЬНЫХ ЭЛЕМЕНТОВ */
uint32_t multiset_unique_count(Multiset* ms) {
    return ms->ht->count;
}

/* ПЕЧАТЬ МУЛЬТИМНОЖЕСТВА */
void multiset_print(Multiset* ms) {
    printf("Multiset { ");
    bool first = true;
    for (uint32_t i = 0; i < ms->ht->size; i++) {
        HashNode* curr = ms->ht->buckets[i];
        while (curr) {
            if (!first) printf(", ");
            printf("%s:%d", curr->key, curr->value);
            first = false;
            curr = curr->next;
        }
    }
    printf(" } (unique=%u, total=%u)\n",
        multiset_unique_count(ms), multiset_total_size(ms));
}

/* ОСВОБОЖДЕНИЕ МУЛЬТИМНОЖЕСТВА */
void multiset_destroy(Multiset* ms) {
    if (!ms) return;
    ht_destroy(ms->ht);
    free(ms);
}


/* АВТОМАТИЧЕСКИЕ ТЕСТЫ */
void test_hash_table() {
    printf("\n=== ТЕСТ ХЕШ-ТАБЛИЦЫ ===\n");

    HashTable* ht = ht_create(10, fnv1a_hash);

    /* Вставка элементов */
    ht_insert(ht, "Иван", 5);
    ht_insert(ht, "Даниил", 7);
    ht_insert(ht, "Олег", 3);
    ht_insert(ht, "Михаил", 9);
    ht_insert(ht, "Петр", 2);
    ht_insert(ht, "roman", 6);
    ht_insert(ht, "date", 8);

    ht_print(ht);

    /* Поиск значения */
    int val;
    if (ht_get(ht, "Иван", &val)) {
        printf("Найдено 'Иван': %d\n", val);
    }

    /* Удаление элемента */
    ht_delete(ht, "roman");
    printf("После удаления 'roman':\n");
    ht_print(ht);

    /* Проверка наличия */
    printf("Содержит 'Петр': %s\n", ht_contains(ht, "Петр") ? "да" : "нет");
    printf("Содержит 'roman': %s\n", ht_contains(ht, "roman") ? "да" : "нет");

    ht_destroy(ht);
}

void test_set() {
    printf("\n=== ТЕСТ МНОЖЕСТВА ===\n");

    Set* s1 = set_create(10);
    Set* s2 = set_create(10);

    /* Формируем множества */
    set_add(s1, "A");
    set_add(s1, "B");
    set_add(s1, "C");
    set_add(s1, "D");

    set_add(s2, "C");
    set_add(s2, "D");
    set_add(s2, "E");
    set_add(s2, "F");

    printf("s1: "); set_print(s1);
    printf("s2: "); set_print(s2);

    /* Объединение */
    Set* s_union = set_create(10);
    set_add(s_union, "A"); set_add(s_union, "B");
    set_add(s_union, "C"); set_add(s_union, "D");
    set_union(s_union, s2);
    printf("s1 ∪ s2: "); set_print(s_union);
    set_destroy(s_union);

    /* Пересечение */
    Set* s_inter = set_create(10);
    set_add(s_inter, "A"); set_add(s_inter, "B");
    set_add(s_inter, "C"); set_add(s_inter, "D");
    set_intersection(s_inter, s2);
    printf("s1 ∩ s2: "); set_print(s_inter);
    set_destroy(s_inter);

    /* Проверка подмножества */
    Set* s3 = set_create(10);
    set_add(s3, "C");
    set_add(s3, "D");
    printf("s3: "); set_print(s3);
    printf("s3 ⊆ s1: %s\n", set_is_subset(s1, s3) ? "да" : "нет");
    printf("s3 ⊆ s2: %s\n", set_is_subset(s2, s3) ? "да" : "нет");

    set_destroy(s1);
    set_destroy(s2);
    set_destroy(s3);
}

void test_multiset() {
    printf("\n=== ТЕСТ МУЛЬТИМНОЖЕСТВА ===\n");

    Multiset* ms = multiset_create(10);

    /* Добавление элементов с повторениями */
    multiset_add(ms, "яблоко");
    multiset_add(ms, "яблоко");
    multiset_add(ms, "банан");
    multiset_add(ms, "банан");
    multiset_add(ms, "банан");
    multiset_add(ms, "вишня");

    multiset_print(ms);

    /* Проверка кратности */
    printf("Кратность 'яблоко': %d\n", multiset_count(ms, "яблоко"));
    printf("Кратность 'банан': %d\n", multiset_count(ms, "банан"));
    printf("Кратность 'апельсин': %d\n", multiset_count(ms, "апельсин"));

    /* Удаление одного экземпляра */
    multiset_remove_one(ms, "яблоко");
    printf("После удаления одного 'яблоко':\n");
    multiset_print(ms);

    /* Добавление нескольких копий */
    multiset_add_n(ms, "груша", 3);
    printf("После добавления 3 'груша':\n");
    multiset_print(ms);

    /* Удаление всех экземпляров */
    multiset_remove_all(ms, "банан");
    printf("После удаления всех 'банан':\n");
    multiset_print(ms);

    multiset_destroy(ms);
}

/* ТЕСТ НА КОЛЛИЗИИ
 * Специально используем маленькую таблицу, чтобы вызвать коллизии.
 * Проверяем, что все данные сохраняются корректно.
*/
void test_collisions() {
    printf("\n=== ТЕСТ КОЛЛИЗИЙ (FNV-1a) ===\n");

    /* Маленькая таблица (4 бакета) — гарантированные коллизии */
    HashTable* ht = ht_create(4, fnv1a_hash);

    /* Вставляем 8 элементов в таблицу размером 4 */
    ht_insert(ht, "a", 1);
    ht_insert(ht, "b", 2);
    ht_insert(ht, "c", 3);
    ht_insert(ht, "d", 4);
    ht_insert(ht, "e", 5);
    ht_insert(ht, "f", 6);
    ht_insert(ht, "g", 7);
    ht_insert(ht, "h", 8);

    printf("Таблица размером 4 с 8 элементами (коллизии неизбежны):\n");
    ht_print(ht);

    /* Проверяем, что все данные сохранились */
    int val;
    printf("Проверка всех элементов:\n");
    for (char c = 'a'; c <= 'h'; c++) {
        char key[2] = { c, '\0' };
        if (ht_get(ht, key, &val)) {
            printf("  %s -> %d\n", key, val);
        }
        else {
            printf("  %s -> НЕ НАЙДЕН!\n", key);
        }
    }

    ht_destroy(ht);
}

/* ТЕСТ ПРОИЗВОДИТЕЛЬНОСТИ FNV-1a */
void test_performance() {
    printf("\n=== ТЕСТ ПРОИЗВОДИТЕЛЬНОСТИ FNV-1a ===\n");

    const int N = 10000;
    char keys[10000][20];

    /* Генерация случайных ключей */
    srand(time(NULL));
    for (int i = 0; i < N; i++) {
        snprintf(keys[i], sizeof(keys[i]), "key_%d", rand() % 5000);
    }

    clock_t start, end;

    /* Тест с FNV-1a */
    HashTable* ht = ht_create(1024, fnv1a_hash);
    start = clock();
    for (int i = 0; i < N; i++) {
        ht_insert(ht, keys[i], i);
    }
    end = clock();
    printf("FNV-1a: %ld мкс на %d вставок\n",
        (long)((double)(end - start) / CLOCKS_PER_SEC * 1000000), N);
    ht_destroy(ht);
}

/* ЗАПУСК ВСЕХ ТЕСТОВ */
void run_all_tests() {
    test_hash_table();
    test_set();
    test_multiset();
    test_collisions();
    test_performance();
}

/* ПРИМЕРЫ ПРИМЕНЕНИЯ */
void example_usage() {
    printf("\n=== ПРИМЕРЫ ПРИМЕНЕНИЯ ===\n");

    /* ПРИМЕР 1: Множество для проверки уникальности слов */
    printf("\n1. Проверка уникальности слов в тексте:\n");
    const char* text[] = { "apple", "banana", "apple", "cherry", "banana", "date" };
    Set* unique = set_create(10);

    for (int i = 0; i < 6; i++) {
        if (set_contains(unique, text[i])) {
            printf("  Слово '%s' уже встречалось!\n", text[i]);
        }
        else {
            set_add(unique, text[i]);
            printf("  Добавлено новое слово: '%s'\n", text[i]);
        }
    }
    printf("  Уникальных слов: %u\n", set_size(unique));
    set_destroy(unique);

    /* ПРИМЕР 2: Мультимножество для подсчёта частоты слов */
    printf("\n2. Подсчёт частоты слов с помощью мультимножества:\n");
    Multiset* freq = multiset_create(10);
    const char* words[] = { "the", "я", "написал", "этот", "текст",
                           "текст", "the", "lazy", "этот", "the" };

    /* Подсчёт частоты */
    for (int i = 0; i < 10; i++) {
        multiset_add(freq, words[i]);
    }

    printf("Результат подсчёта:\n");
    multiset_print(freq);

    printf("Частота слова 'the': %d\n", multiset_count(freq, "the"));
    printf("Частота слова 'quick': %d\n", multiset_count(freq, "quick"));
    multiset_destroy(freq);

    /* ПРИМЕР 3: Система прав доступа (множество администраторов) */
    printf("\n3. Система прав доступа:\n");
    Set* admins = set_create(10);
    set_add(admins, "Иван");
    set_add(admins, "bob");
    set_add(admins, "charlie");

    const char* users[] = { "Иван", "Петр", "Даниил", "bob" };
    for (int i = 0; i < 4; i++) {
        printf("  Пользователь %s %s администратором\n",
            users[i], set_contains(admins, users[i]) ? "является" : "не является");
    }
    set_destroy(admins);

    /* ПРИМЕР 4: Операции над множествами (анализ данных) */
    printf("\n4. Анализ данных с помощью операций над множествами:\n");

    Set* students_A = set_create(10);
    Set* students_B = set_create(10);

    set_add(students_A, "Иван");
    set_add(students_A, "Пётр");
    set_add(students_A, "Мария");
    set_add(students_A, "Анна");

    set_add(students_B, "Мария");
    set_add(students_B, "Анна");
    set_add(students_B, "Сергей");
    set_add(students_B, "Елена");

    printf("Группа A: "); set_print(students_A);
    printf("Группа B: "); set_print(students_B);

    /* Студенты, которые есть в обеих группах (пересечение) */
    Set* both = set_create(10);
    set_add(both, "Мария");
    set_add(both, "Анна");
    set_intersection(both, students_A);
    set_intersection(both, students_B);
    printf("Студенты в обеих группах: "); set_print(both);
    set_destroy(both);

    /* Студенты только в группе A (разность) */
    Set* only_A = set_create(10);
    set_add(only_A, "Иван");
    set_add(only_A, "Пётр");
    set_add(only_A, "Мария");
    set_add(only_A, "Анна");
    set_difference(only_A, students_B);
    printf("Только в группе A: "); set_print(only_A);
    set_destroy(only_A);

    set_destroy(students_A);
    set_destroy(students_B);
}

/*  MAIN */
int main() {
    setlocale(0, "Rus");
    printf("===============================================================\n");
    printf("     ХЕШ-ТАБЛИЦА, МНОЖЕСТВО И МУЛЬТИМНОЖЕСТВО НА C\n");
    printf("     Хеш-функция: FNV-1a (Fowler-Noll-Vo, вариант 1a)\n");
    printf("     Разрешение коллизий: цепочки\n");
    printf("===============================================================\n");

    /* Запуск всех тестов */
    run_all_tests();

    /* Демонстрация примеров применения */
    example_usage();

    printf("\n=== ВСЕ ТЕСТЫ УСПЕШНО ЗАВЕРШЕНЫ ===\n");
    return 0;
}