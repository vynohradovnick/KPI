#include "main.h"
#include "stage_journal.h"

#define JOURNAL_PAGE_0 14U
#define JOURNAL_PAGES 2U
#define SLOTS (FLASH_PAGE_SIZE / sizeof(StageRecord))

_Static_assert(sizeof(StageRecord) == 16U, "journal record layout");

static uint32_t address(unsigned page, unsigned slot)
{
    return FLASH_BASE + (JOURNAL_PAGE_0 + page) * FLASH_PAGE_SIZE +
           slot * sizeof(StageRecord);
}

static StageRecord read_slot(unsigned page, unsigned slot)
{
    const volatile StageRecord *flash = (const volatile StageRecord *)address(page, slot);
    return *flash;
}

static uint32_t seal(const StageRecord *r)
{
    const uint32_t fields[] = {r->number, r->uptime_ms, r->event};
    uint32_t crc = 0xffffffffU;
    for (unsigned i = 0; i < 3U; ++i) {
        uint32_t word = fields[i];
        for (unsigned j = 0; j < 32U; ++j) {
            const uint32_t carry = (crc ^ word) & 1U;
            crc >>= 1;
            if (carry) {
                crc ^= 0xedb88320U;
            }
            word >>= 1;
        }
    }
    /* The final word is programmed in the second doubleword. */
    return crc & 0x7fffffffU;
}

static bool valid(const StageRecord *r)
{
    return r->number != 0U && r->number != UINT32_MAX &&
           r->event >= STAGE_SHOW && r->event <= STAGE_IMU_BACK &&
           r->seal == seal(r);
}

static bool blank(const StageRecord *r)
{
    return (r->number & r->uptime_ms & r->event & r->seal) == UINT32_MAX;
}

static uint32_t latest(unsigned *page_out, unsigned *slot_out)
{
    uint32_t number = 0;
    *page_out = 0;
    *slot_out = 0;
    for (unsigned page = 0; page < JOURNAL_PAGES; ++page) {
        for (unsigned slot = 0; slot < SLOTS; ++slot) {
            const StageRecord r = read_slot(page, slot);
            if (r.number > number && valid(&r)) {
                number = r.number;
                *page_out = page;
                *slot_out = slot;
            }
        }
    }
    return number;
}

uint32_t stage_journal_latest(void)
{
    unsigned page, slot;
    return latest(&page, &slot);
}

bool stage_journal_read(uint32_t number, StageRecord *record)
{
    for (unsigned page = 0; page < JOURNAL_PAGES; ++page) {
        for (unsigned slot = 0; slot < SLOTS; ++slot) {
            const StageRecord r = read_slot(page, slot);
            if (r.number == number && valid(&r)) {
                *record = r;
                return true;
            }
        }
    }
    return false;
}

bool stage_journal_append(uint32_t uptime_ms, enum StageEvent event)
{
    unsigned page, slot;
    const uint32_t previous = latest(&page, &slot);
    if (previous == UINT32_MAX - 1U || HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    if (previous != 0U) {
        ++slot;
    }
    for (; slot < SLOTS; ++slot) {
        const StageRecord r = read_slot(page, slot);
        if (blank(&r)) {
            break;
        }
    }
    bool ok = true;
    if (slot == SLOTS) {
        page ^= 1U;
        FLASH_EraseInitTypeDef erase = {
            .TypeErase = FLASH_TYPEERASE_PAGES, .Page = JOURNAL_PAGE_0 + page, .NbPages = 1
        };
        uint32_t failed_page = 0;
        ok = HAL_FLASHEx_Erase(&erase, &failed_page) == HAL_OK;
        slot = 0;
    }
    StageRecord r = { .number = previous + 1U, .uptime_ms = uptime_ms, .event = event };
    r.seal = seal(&r);
    const uint32_t target = address(page, slot);
    if (ok) {
        const uint64_t first = ((uint64_t)r.uptime_ms << 32) | r.number;
        const uint64_t second = ((uint64_t)r.seal << 32) | r.event;
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, target, first) == HAL_OK &&
             HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, target + 8U, second) == HAL_OK;
    }
    (void)HAL_FLASH_Lock();
    if (!ok) {
        return false;
    }
    const StageRecord saved = read_slot(page, slot);
    return valid(&saved) && saved.number == r.number && saved.uptime_ms == r.uptime_ms &&
           saved.event == r.event;
}
