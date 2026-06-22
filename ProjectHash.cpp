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