-- SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
-- SPDX-License-Identifier: LGPL-2.1-or-later
-- Native helpers: words(text), read_dictionary(language), metadata_path().
local native = emoji_native
local metadata = dofile(native.metadata_path())
local known, catalogues = {}, {}
for _, item in ipairs(metadata) do known[item.emoji] = item end

local function parse(data)
    local pos = 1
    local function integer()
        assert(pos + 3 <= #data, 'Truncated emoji dictionary')
        local value
        value, pos = string.unpack('<I4', data, pos)
        return value
    end
    local function text()
        local size = integer()
        assert(pos + size - 1 <= #data, 'Truncated emoji string')
        local value = data:sub(pos, pos + size - 1)
        assert(utf8.len(value), 'Invalid UTF-8 in emoji dictionary')
        pos = pos + size
        return value
    end
    local rows, count = {}, integer()
    assert(count <= 100000, 'Invalid emoji count')
    for _ = 1, count do
        local glyph, annotations, n = text(), {}, integer()
        assert(n <= 10000, 'Invalid annotation count')
        for _ = 1, n do annotations[#annotations + 1] = text() end
        rows[#rows + 1] = {emoji=glyph, annotations=annotations}
    end
    assert(pos == #data + 1, 'Trailing emoji dictionary data')
    return rows
end

local function load(language)
    if catalogues[language] then return catalogues[language] end
    -- The native reader merges regional -> base -> English by emoji. The
    -- first available language supplies each emoji's annotations.
    local rows, seen = {}, {}
    for _, data in ipairs(native.read_dictionary(language)) do
        for _, row in ipairs(parse(data)) do
            if not seen[row.emoji] then
                seen[row.emoji] = true
                rows[#rows + 1] = row
            end
        end
    end
    local by_glyph = {}
    for _, row in ipairs(rows) do by_glyph[row.emoji] = row end
    for _, item in ipairs(metadata) do
        if item.source ~= '' and by_glyph[item.source] then
            rows[#rows + 1] = {emoji=item.emoji, annotations=by_glyph[item.source].annotations}
        end
    end
    local index, vocabulary = {}, {}
    for order, row in ipairs(rows) do
        row.order = order
        row.meta = known[row.emoji] or {family=row.emoji}
        row.name = row.meta.name or row.annotations[1] or row.emoji
        row.words = {}
        for _, annotation in ipairs(row.annotations) do
            for _, word in ipairs(native.words(annotation)) do row.words[word] = true end
        end
        -- CLDR dictionaries omit a separate display-name field (country flags
        -- often contain only "flag"). Supplement, never replace, their words
        -- with the canonical Unicode label so named queries remain possible.
        for _, word in ipairs(native.words(row.name)) do row.words[word] = true end
        for word in pairs(row.words) do
            if not index[word] then index[word] = {}; vocabulary[#vocabulary+1] = word end
            index[word][#index[word]+1] = order
        end
    end
    table.sort(vocabulary)
    local catalogue = {rows=rows, index=index, vocabulary=vocabulary}
    catalogues[language] = catalogue
    return catalogue
end

local function lower_bound(values, term)
    local low, high = 1, #values + 1
    while low < high do
        local mid = (low + high) // 2
        if values[mid] < term then low = mid + 1 else high = mid end
    end
    return low
end

local function preference(row, config, explicit)
    local score, demoted, m = 0, false, row.meta
    local function prefer(key, field, weight)
        local desired, actual = config[key], m[field]
        if desired and desired ~= 'Any' and actual and actual ~= '' and not explicit[field] then
            if desired == actual then score = score + weight else demoted = true end
        end
    end
    prefer('Gender', 'gender', 16)
    prefer('SecondGender', 'gender2', 8)
    prefer('SkinTone', 'tone', 4)
    local second = config.SecondSkinTone
    if second == 'Inherit' then
        local copy = setmetatable({SecondSkinTone=config.SkinTone}, {__index=config})
        config = copy
    end
    prefer('SecondSkinTone', 'tone2', 2)
    prefer('Hair', 'hair', 1)
    prefer('Direction', 'direction', 1)
    prefer('Presentation', 'presentation', 1)
    prefer('Country', 'country', 1)
    return score, demoted
end

local M = {}
function M.search(query, config)
    local catalogue = load(config.Language or 'en')
    local terms, seen = {}, {}
    for _, word in ipairs(native.words(query)) do
        if not seen[word] then terms[#terms+1] = word; seen[word] = true end
    end
    local candidates
    for _, term in ipairs(terms) do
        local matches = {}
        local first = lower_bound(catalogue.vocabulary, term)
        for i = first, #catalogue.vocabulary do
            local word = catalogue.vocabulary[i]
            if word:sub(1, #term) ~= term then break end
            for _, id in ipairs(catalogue.index[word]) do
                matches[id] = math.max(matches[id] or 0, word == term and 2 or 1)
            end
        end
        if not candidates then candidates = matches else
            for id, score in pairs(candidates) do
                if matches[id] then candidates[id] = score + matches[id] else candidates[id] = nil end
            end
        end
    end
    if not candidates then
        candidates = {}
        for id in ipairs(catalogue.rows) do candidates[id] = 0 end
    end
    -- Explicit variant queries take precedence. Their own annotations already
    -- determine eligibility and match quality; do not counter-rank them.
    local explicit = {}
    local controls = {gender={'man','woman','person','male','female'},
                      tone={'skin','tone','light','medium','dark'},
                      hair={'hair','bald','red','curly','white'},
                      direction={'left','right'}, presentation={'text','emoji'}}
    for field, words in pairs(controls) do
        for _, term in ipairs(terms) do
            for _, word in ipairs(words) do
                if word:sub(1, #term) == term then explicit[field] = true end
            end
        end
    end
    explicit.gender2, explicit.tone2 = explicit.gender, explicit.tone
    local groups = {}
    for id, relevance in pairs(candidates) do
        local row = catalogue.rows[id]
        local key = tostring(relevance) .. '\0' .. row.meta.family
        if not groups[key] then groups[key] = {score=relevance, order=id, rows={}} end
        local group = groups[key]
        group.order = math.min(group.order, id)
        local score, demoted = preference(row, config, explicit)
        group.rows[#group.rows+1] = {row=row, preference=score, demoted=demoted}
    end
    -- First reorder equally relevant family members in their original slots.
    -- Then move alternatives conflicting with a preference behind all other
    -- matches, so one family's variants cannot bury unrelated results.
    local ordered = {}
    for id, relevance in pairs(candidates) do
        ordered[#ordered+1] = {id=id, score=relevance}
    end
    table.sort(ordered, function(a,b)
        if a.score ~= b.score then return a.score > b.score end
        return a.id < b.id
    end)
    for _, group in pairs(groups) do
        table.sort(group.rows, function(a,b)
            if a.preference ~= b.preference then return a.preference > b.preference end
            return a.row.order < b.row.order
        end)
        group.next = 1
    end
    local result, alternatives = {}, {}
    for _, item in ipairs(ordered) do
        local row = catalogue.rows[item.id]
        local group = groups[tostring(item.score) .. '\0' .. row.meta.family]
        local entry = group.rows[group.next]
        local chosen = entry.row
        group.next = group.next + 1
        local destination = entry.demoted and alternatives or result
        destination[#destination+1] = {chosen.emoji, chosen.name}
    end
    for _, row in ipairs(alternatives) do result[#result+1] = row end
    return result
end
function M.clear() catalogues = {} end
M.parse = parse
return M
