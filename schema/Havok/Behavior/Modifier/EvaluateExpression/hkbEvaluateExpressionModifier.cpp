/// @file
/// @brief hkbEvaluateExpressionModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/EvaluateExpression/hkbEvaluateExpressionModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Event/hkbEventQueue.h>

namespace CB { namespace schema
{
    hkbExpressionDataArray::hkbExpressionDataArray(const hkbExpressionDataArray& other)
    :   hkReferencedObject()
    {
        m_expressionsData = other.m_expressionsData;
    }

    hkbEvaluateExpressionModifier::hkbEvaluateExpressionModifier(const hkbEvaluateExpressionModifier& mod)
    :   hkbModifier(mod),
        m_compiledExpressionSet(mod.m_compiledExpressionSet)
    {
        m_userData |= 2;
        m_expressions.setAndDontIncrementRefCount(new hkbExpressionDataArray(*mod.m_expressions));
    }

    const void* hkbEvaluateExpressionModifier::getClassType() const
    {
        return &staticClass();
    }

    /// @note Every expression starts with its event unraised and false.
    void hkbEvaluateExpressionModifier::activate(const hkbContext& context)
    {
        HK_TIMER_BEGIN("hkbEvaluateExpressionModifier::activate");

        if (m_expressions) {
            const int numExpressions = m_expressions->m_expressionsData.getSize();
            for (int i = 0; i < numExpressions; i++) {
                hkbExpressionData& data       = m_expressions->m_expressionsData[i];
                data.m_raisedEvent            = false;
                data.m_wasTrueInPreviousFrame = false;
            }
        }

        HK_TIMER_END();
    }

    /// @note An expression that fails to evaluate changes nothing. The event goes out with no payload and no sender.
    void hkbEvaluateExpressionModifier::update(const hkbContext& context, hkReal timestep)
    {
        if (!m_expressions || m_expressions->m_expressionsData.getSize() == 0) return;

        HK_TIMER_BEGIN("hkbEvaluateExpressionModifier::update");

        hkbEventQueue* queue = context.getEventQueue();
        for (int i = 0; i < m_expressions->m_expressionsData.getSize(); i++) {
            hkReal result = 0.0f;
            if (!m_compiledExpressionSet->evaluate(context, i, result)) continue;

            const int variableIndex = m_expressions->m_expressionsData[i].m_assignmentVariableIndex;
            if (variableIndex != -1) setVariable(variableIndex, context, result);

            hkbExpressionData& data = m_expressions->m_expressionsData[i];
            const int          eventIndex = data.m_assignmentEventIndex;
            const hkInt8       eventMode  = data.m_eventMode;
            if (eventIndex != -1) {
                if ((eventMode == hkbExpressionData::EVENT_MODE_SEND_ONCE && !data.m_raisedEvent && result > 0.0f) ||
                    (eventMode == hkbExpressionData::EVENT_MODE_SEND_ON_TRUE && result > 0.0f) ||
                    (eventMode == hkbExpressionData::EVENT_MODE_SEND_ON_FALSE_TO_TRUE && !data.m_wasTrueInPreviousFrame && result > 0.0f) ||
                    (eventMode == hkbExpressionData::EVENT_MODE_SEND_EVERY_FRAME_ONCE_TRUE && (data.m_raisedEvent || result > 0.0f))) {
                    hkbEvent event(eventIndex, HK_NULL, HK_NULL);
                    event.m_id = queue->toExternal(event.m_id);
                    queue->enqueue(event);
                    data.m_raisedEvent = true;
                }
            }
            data.m_wasTrueInPreviousFrame = result > 0.0f;
        }

        HK_TIMER_END();
    }

    /// @note Compiles in a scratch set against no names (syntax only); the errors of every failing expression are
    ///       gathered, each under its index, into a static buffer the error string then copies.
    /// @warning Vanilla terminates the gathered text with the character '0' (0x30) instead of a NUL, so the reported
    ///          string runs on into whatever the static buffer held (it starts zeroed, so the first report ends right after
    ///          the stray '0').
    bool hkbEvaluateExpressionModifier::isValid(hkStringPtr& errorString) const
    {
        if (!m_expressions || m_expressions->m_expressionsData.getSize() == 0) return true;

        hkStringBuf              allErrors;
        bool                     valid = true;
        hkbCompiledExpressionSet compiledExpressionSet;
        for (int i = 0; i < m_expressions->m_expressionsData.getSize(); i++) {
            char header[100];
            hkString::sprintf(header, "\n\nEvaluating Expression at index: %d\n", i);
            hkStringBuf errors;
            errors += header;

            hkStringBuf        expression(m_expressions->m_expressionsData[i].m_expression);
            hkbExpressionData& data     = m_expressions->m_expressionsData[i];
            const bool         parsed   = parseAssignment(expression, errors, data.m_assignmentEventIndex, data.m_assignmentVariableIndex, HK_NULL, HK_NULL);
            const bool         compiled = compiledExpressionSet.compile(expression.cString(), HK_NULL, HK_NULL, errors);
            if (!(parsed & compiled)) {
                allErrors += errors.cString();
                valid = false;
            }
        }

        if (!valid) {
            static char s_errorBuffer[1024];
            const int   length = hkMath::min2(allErrors.getLength(), 1023);
            hkString::memCpy(s_errorBuffer, allErrors.cString(), length);
            s_errorBuffer[length] = '0';
            errorString           = s_errorBuffer;
        }
        return valid;
    }

    /// @note Once per template: the compiled set is shared by every clone. Variable and character-property names
    ///       resolve through the context's behavior; errors are collected and dropped.
    void hkbEvaluateExpressionModifier::precompute(const hkbContext& context)
    {
        if (m_compiledExpressionSet) return;

        m_compiledExpressionSet.setAndDontIncrementRefCount(new hkbCompiledExpressionSet());
        if (!m_expressions || m_expressions->m_expressionsData.getSize() == 0) return;

        hkStringMap<int> variableNameToIndex;
        hkStringMap<int> characterPropertyNameToIndex;
        if (context.m_behavior->m_data) {
            const hkbBehaviorGraphStringData* stringData = context.m_behavior->m_data->m_stringData;
            const int numVariables = stringData->m_variableNames.getSize();
            for (int i = 0; i < numVariables; i++) variableNameToIndex.insert(stringData->m_variableNames[i].cString(), i);
            const int numCharacterProperties = stringData->m_characterPropertyNames.getSize();
            for (int i = 0; i < numCharacterProperties; i++) characterPropertyNameToIndex.insert(stringData->m_characterPropertyNames[i].cString(), i);
        }

        hkStringBuf errorString;
        for (int i = 0; i < m_expressions->m_expressionsData.getSize(); i++) {
            hkStringBuf        expression(m_expressions->m_expressionsData[i].m_expression);
            hkbExpressionData& data = m_expressions->m_expressionsData[i];
            parseAssignment(expression, errorString, data.m_assignmentEventIndex, data.m_assignmentVariableIndex, &context, &variableNameToIndex);
            m_compiledExpressionSet->compile(expression.cString(), &variableNameToIndex, &characterPropertyNameToIndex, errorString);
        }
    }

    hkbNode* hkbEvaluateExpressionModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbEvaluateExpressionModifier(*this);
    }

    void hkbEvaluateExpressionModifier::getActiveVariables(hkPointerMap<int, bool>& activeVariables) const
    {
        if (m_compiledExpressionSet) m_compiledExpressionSet->getActiveVariables(activeVariables);

        if (m_expressions) {
            const int numExpressions = m_expressions->m_expressionsData.getSize();
            for (int i = 0; i < numExpressions; i++) {
                const int variableIndex = m_expressions->m_expressionsData[i].m_assignmentVariableIndex;
                if (variableIndex >= 0) activeVariables.insert(variableIndex, true);
            }
        }
    }

    void hkbEvaluateExpressionModifier::getInternalStateUser(const hkbBehaviorGraph& rootBehavior, hkReferencedObject& internalState)
    {
        if (!m_expressions) return;

        hkbEvaluateExpressionModifierInternalState& state = static_cast<hkbEvaluateExpressionModifierInternalState&>(internalState);
        const int numExpressions = m_expressions->m_expressionsData.getSize();
        state.m_internalExpressionsData.setSize(numExpressions);
        for (int i = 0; i < numExpressions; i++) {
            const hkbExpressionData&                             data  = m_expressions->m_expressionsData[i];
            hkbEvaluateExpressionModifierInternalExpressionData& saved = state.m_internalExpressionsData[i];
            saved.m_raisedEvent            = data.m_raisedEvent;
            saved.m_wasTrueInPreviousFrame = data.m_wasTrueInPreviousFrame;
        }
    }

    /// @note Restores from the state's copy, then empties the modifier's own (setInternalState fills it).
    void hkbEvaluateExpressionModifier::setInternalStateUser(const hkbContext& context, const hkReferencedObject& internalState,
                                                             hkPointerMap<hkInt16, const hkbNodeInternalStateInfo*>& nodeIdToInternalStateMap)
    {
        const hkbEvaluateExpressionModifierInternalState& state = static_cast<const hkbEvaluateExpressionModifierInternalState&>(internalState);
        const int numExpressions = m_expressions->m_expressionsData.getSize();
        for (int i = 0; i < numExpressions; i++) {
            hkbExpressionData&                                         data  = m_expressions->m_expressionsData[i];
            const hkbEvaluateExpressionModifierInternalExpressionData& saved = state.m_internalExpressionsData[i];
            data.m_raisedEvent            = saved.m_raisedEvent;
            data.m_wasTrueInPreviousFrame = saved.m_wasTrueInPreviousFrame;
        }
        m_internalExpressionsData.clear();
    }

    /// @note `expression`'s assignment is consumed (up to and including " if" / "="); without one, only a syntax check
    ///       (no context, no map) reports it missing.
    bool hkbEvaluateExpressionModifier::parseAssignment(hkStringBuf& expression, hkStringBuf& errorString, hkInt32& eventIndexOut,
                                                        hkInt32& variableIndexOut, const hkbContext* context, hkStringMap<int>* variableNameToIndex)
    {
        bool ok = true;
        expression.replace('\t', ' ');
        expression.replace('\n', ' ');

        int index = expression.indexOf(" if ");
        if (index != -1 || (index = expression.indexOf(" if(")) != -1) {
            hkStringBuf eventName(expression.cString(), index);
            trimWhitespace(eventName);
            expression.chompStart(index + 3);
            if (context) {
                eventIndexOut = findEventIndex(context->m_behavior, eventName.cString());
                if (eventIndexOut == -1) {
                    errorString += "Invalid event name: ";
                    errorString += eventName.cString();
                    errorString += "\n";
                    ok = false;
                }
            }
            return ok;
        }

        index = expression.indexOf('=');
        if (index != -1) {
            hkStringBuf variableName(expression.cString(), index);
            trimWhitespace(variableName);
            expression.chompStart(index + 1);
            if (variableNameToIndex) {
                int variableIndex;
                if (variableNameToIndex->get(variableName.cString(), &variableIndex) == HK_SUCCESS) {
                    variableIndexOut = variableIndex;
                } else {
                    errorString += "Invalid variable name: ";
                    errorString += variableName.cString();
                    errorString += "\n";
                    ok = false;
                }
            }
            return ok;
        }

        if (context) return true;
        if (variableNameToIndex) return true;
        errorString += "assignmentStr = is missing\n";
        return false;
    }

    int hkbEvaluateExpressionModifier::findEventIndex(const hkbBehaviorGraph* behavior, const char* eventName)
    {
        const hkbBehaviorGraphStringData* stringData = behavior->m_data->m_stringData;
        const int                         numEvents  = stringData->m_eventNames.getSize();
        for (int i = 0; i < numEvents; i++) {
            if (hkString::strCmp(eventName, stringData->m_eventNames[i].cString()) == 0) return i;
        }
        return -1;
    }

    namespace
    {
        /// @brief A word value read as T (a real through an integer copy, as vanilla converts it).
        template <typename T> HK_FORCE_INLINE T getWord(const hkbVariableValue& v) { return *reinterpret_cast<const T*>(&v.m_value); }
        template <> HK_FORCE_INLINE hkReal getWord<hkReal>(const hkbVariableValue& v)
        {
            union { hkInt32 i; hkReal r; } u;
            u.i = v.m_value;
            return u.r;
        }
        template <typename T> HK_FORCE_INLINE void setWord(hkbVariableValue& v, T value) { v.m_value = value; }
        template <> HK_FORCE_INLINE void setWord<hkReal>(hkbVariableValue& v, hkReal value)
        {
            union { hkInt32 i; hkReal r; } u;
            u.r       = value;
            v.m_value = u.i;
        }

        /// @brief `value` clamped to [minValue, maxValue].
        /// @note Two spellings for one behaviour: integers select the upper bound with a conditional move, reals branch
        ///       to it (the integer spelling compiles to minss on a real).
        template <typename T> HK_FORCE_INLINE T clampWord(T value, T minValue, T maxValue)
        {
            if (value < minValue) return minValue;
            return (value > maxValue) ? maxValue : value;
        }
        template <> HK_FORCE_INLINE hkReal clampWord<hkReal>(hkReal value, hkReal minValue, hkReal maxValue)
        {
            if (value < minValue) return minValue;
            return (value <= maxValue) ? value : maxValue;
        }

        /// @brief Writes `value` to word variable `index` of `root`, clamped to its bounds when `clamp`.
        template <typename T> HK_FORCE_INLINE void setVariableValueWord(hkbBehaviorGraph* root, int index, T value, bool clamp)
        {
            if (clamp) {
                const T maxValue = getWord<T>(root->m_data->m_wordMaxVariableValues[index]);
                const T minValue = getWord<T>(root->m_data->m_wordMinVariableValues[index]);
                value = clampWord<T>(value, minValue, maxValue);
            }
            setWord<T>(root->m_variableValueSet->m_wordVariableValues[index], value);
        }
    }

    /// @note The type comes from the context's behavior, the bounds and the value set from the root graph (at the
    ///       variable's root index); a root without bounds data does not clamp.
    void hkbEvaluateExpressionModifier::setVariable(int variableIndex, const hkbContext& context, hkReal value)
    {
        const hkbBehaviorGraphData* data      = context.m_behavior->m_data;
        const int                   rootIndex = hkbSymbolIdMap::convertInternalId(context.m_behavior->m_variableIdMap, context.m_character->m_behaviorGraph->m_variableIdMap, variableIndex);
        hkbBehaviorGraph*           root      = context.m_character->m_behaviorGraph;
        bool                        clamp     = false;
        if (root->m_data->m_wordMinVariableValues.getSize() > 0) clamp = true;

        switch (data->m_variableInfos[variableIndex].m_type) {
            case hkbVariableInfo::VARIABLE_TYPE_BOOL:  setVariableValueWord<bool>(root, rootIndex, value != 0.0f, false); break;
            case hkbVariableInfo::VARIABLE_TYPE_INT8:  setVariableValueWord<hkInt8>(root, rootIndex, static_cast<hkInt8>(static_cast<int>(value)), clamp); break;
            case hkbVariableInfo::VARIABLE_TYPE_INT16: setVariableValueWord<hkInt16>(root, rootIndex, static_cast<hkInt16>(static_cast<int>(value)), clamp); break;
            case hkbVariableInfo::VARIABLE_TYPE_INT32: setVariableValueWord<hkInt32>(root, rootIndex, static_cast<hkInt32>(value), clamp); break;
            case hkbVariableInfo::VARIABLE_TYPE_REAL:  setVariableValueWord<hkReal>(root, rootIndex, value, clamp); break;
        }
    }


    void hkbEvaluateExpressionModifier::trimWhitespace(hkStringBuf& s)
    {
        int numLeading = 0;
        for (const char* c = s.cString(); *c == ' '; c++) numLeading++;
        if (numLeading > 0) s.slice(numLeading, s.getLength() - numLeading);

        const int last = s.getLength() - 1;
        int       end  = last;
        for (const char* c = s.cString() + last; *c == ' '; c--) end--;
        if (end < last) s.slice(0, end + 1);
    }

    hkReferencedObject* hkbEvaluateExpressionModifier::createInternalState()
    {
        return new hkbEvaluateExpressionModifierInternalState;
    }

    void hkbEvaluateExpressionModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbEvaluateExpressionModifierInternalState& state = static_cast<hkbEvaluateExpressionModifierInternalState&>(internalState);
        state.m_internalExpressionsData                   = m_internalExpressionsData;
    }

    void hkbEvaluateExpressionModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbEvaluateExpressionModifierInternalState& state = static_cast<const hkbEvaluateExpressionModifierInternalState&>(internalState);
        m_internalExpressionsData                               = state.m_internalExpressionsData;
    }
} }
